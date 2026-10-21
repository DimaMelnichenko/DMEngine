////////////////////////////////////////////////////////////////////////////////
// Мелкая вода в окне у камеры (NearWaterFlow, режим «Shallow water»; docs/water.md, «Мелкая вода у камеры»): вместо
// течения под «жёсткой крышкой» (water_flow.cs с FLOW_NEAR) — уравнения мелкой воды, высота воды переменна, давление
// гидростатическое (его даёт наклон поверхности), решателя давления нет: бугор перед камнем, просадка за ним, волны,
// гидравлический прыжок под ступенью и отражение от бортов получаются сами. Схема — та же, что у эталона на CPU
// (ShallowWater, Scene/Water/ShallowWater.cpp, тесты tests/test_shallow_water.cpp): сетка MAC, явная схема, сохраняющая
// импульс (Stelling, Duinmeijer 2003), подшаг:
//   mainShallowFlux     — расход через грани: глубина ячейки, откуда течёт, × скорость; поток из ячейки — не больше воды
//                         в ней (доля исходящих потоков — по её граням);
//   mainShallowDepth    — высота по расходам;
//   mainShallowVelocity — перенос импульса по расходам вокруг грани, сила тяжести по наклону поверхности, трение по
//                         Маннингу неявно; грань, откуда течь нечему, — без скорости.
// Окно (ячейка g_nearStep, сторона g_nearSize, адрес по модулю стороны — water_near_grid.sh) вложено в детальную воду 0,25 м:
//   - дно — детальная земля билинейно со смещением дисплейсмента слоёв материала (nearDisplacement: камни осыпей и скал,
//     галька — та же функция, что у вершины рельефа, terrainDisplacement): вода обходит и прячется между теми камнями, что
//     рисует рельеф (без них на уступах каскада слив шёл сквозь нарисованные валуны — Dimok, 2026-10-21, вариант 1); камни русла — по верху их моделей (StoneTop), камень проницаем: до shallowStoneSolid
//     глубины детальной воды он — дно (вода над низким камнем ускоряется, перед ним бугор, над ним просадка), выше —
//     сопротивление его доли столба воды (g_nearStoneDrag, Форхгеймер): вода обходит камень и встаёт перед ним, а сквозь ряд
//     камней без просвета сочится — сплошные камни на сетке 6,25 см закрывали щели между ними, и за каждым рядом вставал
//     плёс на 10–13 см выше детальной воды (Dimok, 2026-10-21: «проницаемые камни»). Поверхность в камне закрывает сам камень;
//   - край окна: ячейки в shallowFixedCells от края закреплены — глубина и скорость граней детальной воды (вход и выход
//     расхода, как закреплённые ячейки эталона); вошедшие в окно ячейки начинаются с неё же (mainShallowInit); ячейки за
//     краем детальных плиток (там вода сетки 1 м) закреплены так же — по воде сетки 1 м (coarseWaterAt), и течение окна идёт
//     через край плиток, а не упирается в него;
//   - полоса гашения волн shallowSpongeWidth у края: глубина и скорость тянутся к детальной воде, сильнее к краю
//     (relaxation zone), — волна изнутри уходит в неё, а не отражается от закреплённого края; так же — у края набора
//     детальных плиток внутри окна (за ним вода сетки 1 м, ячейки там — стена): иначе течение пруда упиралось в стену, и у
//     края плитки вода копилась бугром;
//   - крупный масштаб — к детальной воде (mainShallowBlocks, mainShallowOutput): средняя по блокам разница глубины,
//     сглаженная в радиусе shallowNudgeRadius, за shallowNudgeTime (доля кадра — g_nearNudgeShare), — уровень окна не
//     уходит от уровня сети ручьёв, а бугры и ямы размером с камень — свои.
// Шаги — по времени игры (подшаги — NearWaterFlow::stepShallow), с -freezetime их нет: окно — детальная вода.
// Итог (mainShallowOutput) — SLOT_WATER_NEAR в том же виде, что у «жёсткой крышки»: x — отклонение поверхности от уровня
// детальной воды, м, y, z — течение по X и Z мира, w — доля глубины над камнем (как у крышки; 0 — сухо)
////////////////////////////////////////////////////////////////////////////////

#include "water_simulation.sh"
#include "water_near_grid.sh"
#include "water_flow_constants.h"
#include "terrain_layers.sh"	// дисплейсмент земли — ресурсы сцены террейна

// x — дно, м: земля и сплошная часть камня; y — глубина детальной воды над ним (её уровень − дно; −1e9 — детальной воды нет);
// z — проницаемая часть камня над дном, м (0 — камня нет); w — сплошная часть камня под дном, м
DM_UAV( RWTexture2D<float4>, g_cells, 0 );
DM_UAV( RWTexture2D<float>, g_depth, 1 );			// глубина, м
DM_UAV( RWTexture2D<float2>, g_velocity, 2 );		// скорость на восточной (x) и северной (y, к +Z) грани ячейки, м/с
DM_UAV( RWTexture2D<float2>, g_velocityNext, 3 );	// она же после подшага (NearWaterFlow меняет их местами)
DM_UAV( RWTexture2D<float2>, g_flux, 4 );			// расход через восточную и северную грань в этом подшаге, м²/с
DM_UAV( RWTexture2D<float4>, g_output, 5 );			// итог для шейдеров (SLOT_WATER_NEAR)
// x, y — скорость детальной воды на восточной и северной грани, м/с (за краем детальных плиток — воды сетки 1 м); z — 1 у
// ручья (трение по n русел), 0 у озера; w — доля окна у края набора детальных плиток (nearDetailEdge: 1 — далеко от края,
// 0 — на краю), −1 — ячейка за краем плиток (закреплена по воде сетки 1 м)
DM_UAV( RWTexture2D<float4>, g_detail, 6 );
DM_UAV( RWTexture2D<float2>, g_blocks, 7 );			// блок 8 × 8 окна: сумма разницы глубины детальной воды и своей, ячеек

static const float shallowGravity = 9.81f;		// м/с²
static const float shallowMinDepth = 1e-4f;		// мельче — сухо: течь неоткуда, м (ShallowWater::Settings::minDepth)
static const float shallowWetDepth = 1e-3f;		// глубже — вода для итога и подтягивания, м
static const float shallowMaxSpeed = 8.0f;		// скорость грани не больше, м/с
static const int shallowFixedCells = 2;			// ячеек у края окна — детальная вода (закреплены)
static const float shallowSpongeWidth = 1.0f;	// м: полоса гашения волн у края окна (там же к краю гаснет и итог, nearWaterFade)
static const float shallowSpongeRate = 20.0f;	// 1/с: у самого края глубина и скорость тянутся к детальной воде так быстро
static const float shallowNudgeRadius = 1.5f;	// м: подтягивание крупного масштаба — средняя разница в этом радиусе
static const float shallowDryMargin = 0.01f;	// м: у сухой ячейки окна, где детальная вода есть, поверхность — на столько под дном
// Камень — дно не выше этой доли глубины детальной воды над землёй, выше — проницаем (как у детальной сетки: stoneDepthShare
// в water_flow.cs)
static const float shallowStoneSolid = 0.5f;
static const float shallowWallBed = 1e9f;	// дно ячейки без детальной плитки, м: стена (течь в неё неоткуда — она выше всего)

static const int2 axes[2] = { int2( 1, 0 ), int2( 0, 1 ) };

// Ячейка счёта: группа — блок 8 × 8 окна по строкам блоков, поток — ячейка
bool shallowCell( uint3 group, uint3 thread, out int2 cell, out uint2 address )
{
	cell = 0;
	address = 0;
	if( group.x >= g_flowTileCount )
		return false;
	const uint blocks = g_nearSize / 8;
	cell = g_nearOrigin + int2( uint2( group.x % blocks, group.x / blocks ) * 8 + thread.xy );
	return cellAddress( cell, address );
}

// Ячеек от ячейки до края окна (0 — крайняя)
int edgeCells( int2 cell )
{
	const int2 local = cell - g_nearOrigin;
	return min( min( local.x, local.y ), (int)g_nearSize - 1 - max( local.x, local.y ) );
}

// Закреплённая ячейка: у края окна — детальная вода, за краем детальных плиток — вода сетки 1 м (g_detail.w < 0)
bool fixedCell( int2 cell, uint2 address )
{
	return edgeCells( cell ) < shallowFixedCells || g_detail[address].w < 0.0f;
}

// Ячеек до края окна или набора детальных плиток (доля края плиток edge — g_detail.w) — для полосы гашения
float spongeCells( int2 cell, float edge )
{
	return min( edgeCells( cell ) + 0.5f, max( edge, 0.0f ) * nearDetailEdgeFade / g_nearStep );
}

// Доля подшага, на которую глубина и скорость в полосе гашения тянутся к детальной воде; cells — ячеек до края
float spongeShare( float cells )
{
	const float inside = saturate( 1.0f - cells * g_nearStep / shallowSpongeWidth );
	return 1.0f - exp( -shallowSpongeRate * inside * inside * g_flowTimeStep );
}

uint2 addressOf( int2 cell )
{
	uint2 address;
	cellAddress( cell, address );
	return address;
}

// Расход через грань оси axis, которую хранит ячейка; вне окна — 0
float faceFlux( int2 cell, uint axis )
{
	uint2 address;
	return cellAddress( cell, address ) ? g_flux[address][axis] : 0.0f;
}

// Скорость грани оси axis, которую хранит ячейка; вне окна — fallback
float faceSpeed( int2 cell, uint axis, float fallback )
{
	uint2 address;
	return cellAddress( cell, address ) ? g_velocity[address][axis] : fallback;
}

// Нормаль самой детальной земли — разностями через это, м (как fineNormal в cdlod.sh)
static const float nearFineNormalReach = 0.5f;

// Подъём земли дисплейсментом слоёв материала в точке мира над землёй ground, м: смещение — вдоль нормали самой детальной
// земли, как у вершины рельефа (cdlod.vs; с тем же расстоянием LOD и дальностью — дно совпадает с нарисованной землёй), в дно
// окна — его высота
float nearDisplacement( float2 world, float ground )
{
	const float reach = nearFineNormalReach;
	const float left = detailBed( world - float2( reach, 0.0f ) );
	const float right = detailBed( world + float2( reach, 0.0f ) );
	const float back = detailBed( world - float2( 0.0f, reach ) );
	const float front = detailBed( world + float2( 0.0f, reach ) );
	const bool tiles = min( min( left, right ), min( back, front ) ) > -1e8f;
	const float3 normal = tiles ? normalize( float3( left - right, 2.0f * reach, back - front ) ) : float3( 0.0f, 1.0f, 0.0f );
	const float3 position = float3( world.x, ground, world.y );
	return terrainDisplacement( position, normal, lodDistance( position ) ) * normal.y;
}

// Ячейки и грани окна каждый кадр (окно сдвигается): дно, камень, детальная вода над дном, скорость детальной воды на
// гранях; вошедшие в окно в этом кадре (не было в окне прошлого кадра, g_nearReset — все) и закреплённые — эта вода; за
// краем детальных плиток — земля карты высот и вода сетки 1 м
[numthreads( 8, 8, 1 )]
void mainShallowInit( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	uint2 address;
	if( !shallowCell( group, thread, cell, address ) )
		return;
	const float2 world = ( cell + 0.5f ) * g_nearStep;
	float4 water, still;
	const float flat = detailBed( world );
	const bool tile = flat > -1e8f;
	const float ground = tile ? flat + nearDisplacement( world, flat ) : coarseGround( world );
	const bool wet = tile ? detailWaterAt( world, water ) : coarseWaterAt( world, water );
	const float stone = tile ? nearStoneTop( world, ground ) - ground : 0.0f;
	const bool stream = detailStaticAt( world, still ) && still.w > 0.0f;
	const float solid = min( stone, shallowStoneSolid * ( wet ? max( water.x - ground, 0.0f ) : 0.0f ) );
	// За краем детальных плиток без воды сетки 1 м — стена: с дном −1e9 (плитки нет) вода утекала в неё со скоростью предела,
	// и у края плитки посреди пруда вставала белая стена слива (Dimok, 2026-10-21)
	const float bed = tile || wet ? ground + solid : shallowWallBed;
	const float target = wet ? water.x - bed : -1e9f;
	g_cells[address] = float4( bed, target, stone - solid, solid );
	float2 faces = 0.0f;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
		if( nearWaterAt( ( cell + ( axis == 0 ? float2( 1.0f, 0.5f ) : float2( 0.5f, 1.0f ) ) ) * g_nearStep, water ) )
			faces[axis] = water[1 + axis];
	g_detail[address] = float4( faces, stream ? 1.0f : 0.0f, tile ? nearDetailEdge( world ) : -1.0f );
	const int2 previous = cell - g_nearPreviousOrigin;
	const bool fresh = g_nearReset != 0 || any( previous < 0 ) || any( previous >= (int)g_nearSize );
	if( fresh || fixedCell( cell, address ) )
	{
		g_depth[address] = max( target, 0.0f );
		g_velocity[address] = faces;
	}
}

// Доля исходящих потоков ячейки: за подшаг из неё уходит не больше воды, чем в ней есть (у края окна — без доли)
float outflowScale( int2 cell, uint2 address, float depth )
{
	if( fixedCell( cell, address ) )
		return 1.0f;
	const float2 own = g_velocity[address];
	const float west = g_velocity[addressOf( cell - axes[0] )].x;
	const float south = g_velocity[addressOf( cell - axes[1] )].y;
	const float outflow = ( max( own.x, 0.0f ) + max( -west, 0.0f ) + max( own.y, 0.0f ) + max( -south, 0.0f ) ) * depth * g_flowTimeStep / g_nearStep;
	return outflow > depth && outflow > 0.0f ? depth / outflow : 1.0f;
}

[numthreads( 8, 8, 1 )]
void mainShallowFlux( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	uint2 address;
	if( !shallowCell( group, thread, cell, address ) )
		return;
	const float2 velocity = g_velocity[address];
	float2 flux = 0.0f;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		// Грань к ячейке за краем окна никому не нужна: ячейки края закреплены
		const int2 next = cell + axes[axis];
		uint2 nextAddress;
		if( !cellAddress( next, nextAddress ) )
			continue;
		const float u = velocity[axis];
		const bool fromHere = u > 0.0f;
		const uint2 source = fromHere ? address : nextAddress;
		const float depth = g_depth[source];
		flux[axis] = u * depth * outflowScale( fromHere ? cell : next, source, depth );
	}
	g_flux[address] = flux;
}

[numthreads( 8, 8, 1 )]
void mainShallowDepth( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	uint2 address;
	if( !shallowCell( group, thread, cell, address ) || fixedCell( cell, address ) )
		return;
	const float2 own = g_flux[address];
	const float west = g_flux[addressOf( cell - axes[0] )].x;
	const float south = g_flux[addressOf( cell - axes[1] )].y;
	float depth = max( g_depth[address] - g_flowTimeStep / g_nearStep * ( own.x - west + own.y - south ), 0.0f );
	const float share = spongeShare( spongeCells( cell, g_detail[address].w ) );
	if( share > 0.0f )
		depth = lerp( depth, max( g_cells[address].y, 0.0f ), share );
	g_depth[address] = depth;
}

// Доля столба воды ячейки в проницаемой части камня
float stoneShare( float4 info, float depth )
{
	return info.z > 0.0f ? saturate( info.z / max( depth, shallowMinDepth ) ) : 0.0f;
}

// Перенос импульса в консервативной форме (Stelling, Duinmeijer 2003; ShallowWater::updateVelocity): у грани — расход ячеек
// и углов вокруг неё (средний по их граням), скорость со стороны, откуда он идёт; ∂u/∂t + [Δ(q̄ u*) − u Δq̄] / (h̄ Δx).
// Трение по Маннингу и сопротивление камня — неявно, одним знаменателем
[numthreads( 8, 8, 1 )]
void mainShallowVelocity( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	uint2 address;
	if( !shallowCell( group, thread, cell, address ) )
		return;
	const float dt = g_flowTimeStep;
	const float limit = min( shallowMaxSpeed, 0.5f * g_nearStep / dt );
	const float4 here = g_cells[address];
	const float depthHere = g_depth[address];
	const float2 velocity = g_velocity[address];
	const float2 flux = g_flux[address];
	const float4 detail = g_detail[address];
	float2 result = velocity;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		// Грань между ячейками края (или к ячейке за краем окна) — скорость детальной воды, как есть
		const uint other = 1 - axis;
		const int2 along = axes[axis];
		const int2 across = axes[other];
		const int2 next = cell + along;
		uint2 nextAddress;
		if( !cellAddress( next, nextAddress ) || ( fixedCell( cell, address ) && fixedCell( next, nextAddress ) ) )
			continue;
		const float4 there = g_cells[nextAddress];
		const float depthNext = g_depth[nextAddress];
		const float4 detailNext = g_detail[nextAddress];
		const bool stream = detail.z > 0.0f || detailNext.z > 0.0f;
		const float u = velocity[axis];
		const float depth = 0.5f * ( depthHere + depthNext );
		float advection = 0.0f;
		if( depth > shallowMinDepth )
		{
			// Вдоль оси: ячейки cell и next
			const float qa = 0.5f * ( faceFlux( cell - along, axis ) + flux[axis] );
			const float qb = 0.5f * ( flux[axis] + faceFlux( next, axis ) );
			const float ua = qa > 0.0f ? faceSpeed( cell - along, axis, u ) : u;
			const float ub = qb > 0.0f ? u : faceSpeed( next, axis, u );
			advection += qb * ub - qa * ua - u * ( qb - qa );
			// Поперёк: углы грани с меньшей и с большей стороны другой оси
			const float qd = 0.5f * ( faceFlux( cell - across, other ) + faceFlux( next - across, other ) );
			const float qu = 0.5f * ( flux[other] + faceFlux( next, other ) );
			const float ud = qd > 0.0f ? faceSpeed( cell - across, axis, u ) : u;
			const float uu = qu > 0.0f ? u : faceSpeed( cell + across, axis, u );
			advection += qu * uu - qd * ud - u * ( qu - qd );
			advection /= depth * g_nearStep;
		}
		// Наклон поверхности — разность дна и разность глубин по отдельности: дно — десятки метров, и во float у суммы
		// дна и глубины ошибка ~10 мкм — озеро на неровном дне не стояло бы
		const float slope = ( ( there.x - here.x ) + ( depthNext - depthHere ) ) / g_nearStep;
		float value = u - dt * advection - shallowGravity * dt * slope;
		const float upstream = value > 0.0f ? depthHere : depthNext;
		if( upstream <= shallowMinDepth )
			value = 0.0f;
		else
		{
			const float friction = ( stream ? g_flowStreamFriction : g_flowFriction ) / pow( upstream, 4.0f / 3.0f );
			const float stone = dt * g_nearStoneDrag * 0.5f * ( stoneShare( here, depthHere ) + stoneShare( there, depthNext ) );
			value /= 1.0f + ( friction + stone ) * abs( value );
			value = clamp( value, -limit, limit );
		}
		const float share = spongeShare( 0.5f * ( spongeCells( cell, detail.w ) + spongeCells( next, detailNext.w ) ) );
		if( share > 0.0f )
			value = lerp( value, detail[axis], share );
		result[axis] = value;
	}
	g_velocityNext[address] = result;
}

groupshared float2 g_blockSums[64];

// Разница глубины детальной воды и своей по блоку 8 × 8 (группа потоков) — у ячеек, где вода и там, и там (после подшагов)
[numthreads( 8, 8, 1 )]
void mainShallowBlocks( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint index : SV_GroupIndex )
{
	int2 cell;
	uint2 address;
	const bool valid = shallowCell( group, thread, cell, address );
	float2 sum = 0.0f;
	if( valid && !fixedCell( cell, address ) )
	{
		const float depth = g_depth[address];
		const float target = g_cells[address].y;
		if( depth > shallowWetDepth && target > shallowWetDepth )
			sum = float2( target - depth, 1.0f );
	}
	g_blockSums[index] = sum;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for( uint width = 32; width > 0; width >>= 1 )
	{
		if( index < width )
			g_blockSums[index] += g_blockSums[index + width];
		GroupMemoryBarrierWithGroupSync();
	}
	if( valid && index == 0 )
	{
		const uint blocks = g_nearSize / 8;
		g_blocks[uint2( group.x % blocks, group.x / blocks )] = g_blockSums[0];
	}
}

// Средняя разница глубины детальной воды и своей у ячейки — по блокам в радиусе shallowNudgeRadius (веса — шатёр по
// расстоянию до центра блока: поправка плавная, без ступеней на границах блоков)
float nudgeCorrection( int2 cell )
{
	const int blocks = (int)g_nearSize / 8;
	const float2 position = ( float2( cell - g_nearOrigin ) + 0.5f ) / 8.0f;
	const float radius = max( shallowNudgeRadius / ( 8.0f * g_nearStep ), 1.0f );
	const int2 low = max( int2( floor( position - radius ) ), 0 );
	const int2 high = min( int2( floor( position + radius ) ), blocks - 1 );
	float2 sum = 0.0f;
	[loop] for( int j = low.y; j <= high.y; ++j )
		[loop] for( int i = low.x; i <= high.x; ++i )
		{
			const float2 distance = abs( float2( i, j ) + 0.5f - position ) / radius;
			sum += saturate( 1.0f - distance.x ) * saturate( 1.0f - distance.y ) * g_blocks[uint2( i, j )];
		}
	return sum.y > 0.0f ? sum.x / sum.y : 0.0f;
}

// Подтягивание крупного масштаба (доля кадра g_nearNudgeShare) и итог для шейдеров
[numthreads( 8, 8, 1 )]
void mainShallowOutput( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	uint2 address;
	if( !shallowCell( group, thread, cell, address ) )
		return;
	const float4 info = g_cells[address];
	float depth = g_depth[address];
	if( g_nearNudgeShare > 0.0f && !fixedCell( cell, address ) && depth > shallowWetDepth && info.y > shallowWetDepth )
	{
		depth = max( depth + g_nearNudgeShare * nudgeCorrection( cell ), 0.0f );
		g_depth[address] = depth;
	}
	const bool wet = depth > shallowWetDepth;
	// Течение в центре — средняя граней к мокрым соседям: к сухому вода не течёт (скорость грани 0), и у берега среднее
	// вышло бы вдвое меньше
	float2 velocity = 0.0f;
	if( wet )
		[unroll] for( uint axis = 0; axis < 2; ++axis )
		{
			float sum = 0.0f, faces = 0.0f;
			uint2 neighbor;
			if( cellAddress( cell + axes[axis], neighbor ) && g_depth[neighbor] > shallowWetDepth )
			{
				sum += g_velocity[address][axis];
				faces += 1.0f;
			}
			if( cellAddress( cell - axes[axis], neighbor ) && g_depth[neighbor] > shallowWetDepth )
			{
				sum += g_velocity[neighbor][axis];
				faces += 1.0f;
			}
			velocity[axis] = faces > 0.0f ? sum / faces : 0.0f;
		}
	// Отклонение поверхности от детальной воды; у сухой ячейки, где детальная вода есть, — поверхность под дном (урез — по
	// своей воде), где детальной воды нет — 0. Доля глубины над камнем — как у «жёсткой крышки» (пена и рябь окна): столб над
	// верхом камня / столб над землёй, у камня выше воды — Obstacle passage; сухо — 0
	float surface = wet ? ( info.y > -1e8f ? depth - info.y : 0.0f ) : min( -info.y - shallowDryMargin, 0.0f );
	// К краю набора детальных плиток (за ним вода сетки 1 м, здесь — стена) — детальная вода: отклонение — к нулю, течение — к
	// её течению
	const float2 world = ( cell + 0.5f ) * g_nearStep;
	const float edge = saturate( g_detail[address].w );
	if( edge < 1.0f )
	{
		float4 water;
		surface *= edge;
		velocity = lerp( detailWaterAt( world, water ) ? water.yz : 0.0f, velocity, edge );
	}
	const float column = depth + info.w;
	const float share = wet ? max( depth - info.z, g_nearPassage * column ) / column : 0.0f;
	g_output[address] = float4( surface, velocity, share );
}
