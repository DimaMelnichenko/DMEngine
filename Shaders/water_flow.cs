////////////////////////////////////////////////////////////////////////////////
// Течение статичной воды (режим static) — установившийся расчёт при загрузке. Уровень воды уже известен (одно поле
// ручьёв и озёр, TerrainHydrology), поэтому считается только скорость — в ручьях и озёрах одной областью, под «жёсткой
// крышкой»: осреднённые по глубине уравнения мелкой воды без изменения уровня, в слое у поверхности не толще g_flowLayer
// (ручей растекается по озеру поверху; средняя по всей глубине скорость в разы меньше видимой). Скорость переносится сама
// собой (инерция: струя ручья входит в озеро и затухает, на изгибе и слиянии несётся дальше, а не поворачивает сразу),
// гасится трением дна по Маннингу и вязкостью (перемешивание). Ручей держит сила тяжести вдоль уклона поверхности: при
// скорости по Маннингу вдоль оси русла (растр конвейера — u₀) она ровно равна трению дна, поэтому на ровном участке
// течение — u₀, а у слияний, изгибов и препятствий его меняют инерция и давление. Расход: ∇·(h u) = ∇·(h u₀) — сеть ручьёв
// задаёт, сколько воды где прибывает (притоки, водосбор) и уходит (исток, край карты); давление поправляет только то, что
// поле отошло от u₀, поэтому оно местное (у слияний, впадений, препятствий) и сходится быстро даже в длинных руслах. В
// озере u₀ нет: ∇·(h u) = 0 — вода впадения проходит озеро до истока.
// Сетка разнесённая (MAC, Harlow, Welch 1965): давление — в центре ячейки, скорость — на гранях: тексель (x, y) хранит
// скорость на восточной грани (x + 1, y + ½, по оси x) и на южной (x + ½, y + 1, по оси y; оси — тексели, y — строки,
// против Z мира). Тогда расход через грань один у обеих ячеек, и поправка давлением убирает расхождение точно. Шаг
// (Stam 1999, «Stable Fluids», с весом глубины):
//   mainFlowAdvect     — перенос скорости назад по течению (полулагранжев);
//   mainFlowViscosity  — вязкость по перенесённому полю, трение дна и сила тяжести ручья (вязкость по полю до переноса,
//                        прибавленная к перенесённому, неустойчива при переносе на ячейку за шаг: шахматный узор растёт —
//                        на детальной сетке скорость разгонялась до десятков м/с);
//   mainFlowNudge      — FLOW_DETAIL: крупный масштаб — к решению 1 м (ниже);
//   mainFlowDivergence — расхождение расхода ∇·(h u);
//   mainFlowRed/Black  — давление: ∇·(h ∇p) = ∇·(h u), красно-чёрный Гаусс-Зейдель с верхней релаксацией на месте
//                        (давление прошлого шага — начальное приближение);
//   mainFlowProject    — u − ∇p.
// mainFlowRestore — скорость для шейдеров из кэша на диске (StaticWater::solve) вместо расчёта.
// Ячейки: сухие (берег), озеро и ручей (обе считаются; у ручья — u₀). Грани: закрыта (берег) или свободна (между двумя
// мокрыми); у грани с ручьём — заданная скорость u₀ (два ручья — средняя, ручей и озеро — ручья). Класс WaterSimulation
// (solveFlow), docs/water.md, «Течение»
//
// FLOW_DETAIL (define) — то же течение на детальной сетке у русел (StaticWater::solveDetailFlow, docs/water.md,
// «Детальная вода у русел»): ячейка 0,25 м, сетка мира — по всем плиткам детальной земли сразу (ячейка → плитка по
// индексу детальной земли → срез, тексель с полем), поэтому соседние плитки — одна область без швов; оси — X и Z мира
// (y — по Z, а не против). Отличия от сетки 1 м:
//   - начало — решение 1 м (струи, слияния, озёра в нём уже есть), на грани с ячейкой вне плиток (там вода по сетке 1 м) —
//     его скорость (грань задана); счёт — только блоки 8 × 8 с водой и соседние (список — WaterSimulation);
//   - камни русла — геометрия: дно поднято до поверхности камня, но камень отнимает не больше половины глубины
//     (stoneDepthShare): у камня уровень поднимается (подпор), а уровень сети о камнях не знает, и под «жёсткой крышкой»
//     камень выше воды перекрыл бы узкий ручей (видимая вода — 2–3 ячейки). Проводимость грани — глубина: вода обходит
//     камень, над ним мельче и медленнее (трение), рядом быстрее. Расход сети ∇·(h₀ u₀) — по глубине без камней h₀: у
//     камня вода уходит в обход, а не пропадает;
//   - сила тяжести ручья — по глубине без камней (h₀): равновесие — u₀ (h / h₀)^(2/3), над камнем медленнее; профиль
//     поперёк русла (у оси быстрее, у берегов медленнее) — уже в u₀ (TerrainHydrology: u₀ × h₀^(2/3) с прежним расходом
//     сечения), иначе расход сети ∇·(h₀ u₀) с ровным u₀ поперёк сечения выравнивал бы скорость давлением;
//   - крупный масштаб тянется к решению 1 м (mainFlowNudge, spectral nudging вложенных моделей, von Storch, Langenberg,
//     Feser 2000): средняя скорость блока 8 × 8 (2 м) по глубине — к средней сетки 1 м за nudgeTime, мелкий узор (обход
//     камней, профиль поперёк русла, струи) — свой. Давление под «жёсткой крышкой» поправляет расход только вблизи, и
//     камни подряд (запруда за запрудой, течение не успевает разогнаться) тормозили весь ручей втрое, а сетка 1 м камни
//     уже держит сопротивлением с возмещением (в среднем — по Маннингу)
//
// FLOW_NEAR (define) — то же течение в окне вокруг камеры каждый кадр (NearWaterFlow, docs/water.md, «Подробное течение у
// камеры»): ячейка g_nearStep (6,25 см), окно g_nearSize² привязано к миру, адрес ячейки — по модулю стороны окна (окно
// сдвигается, ячейки остаются на месте; вошедшие начинаются с течения детальной сетки — mainFlowFaces). Вложено в детальную
// сетку, как она — в сетку 1 м: ячейка — из детальных плиток билинейно (уровень, u₀, R, дно), за окном — ячейки вне
// (грань задана течением детальной сетки), крупный масштаб тянется к нему (mainFlowNudge). Отличия:
//   - камни русла — полуэллипсоиды на своей сетке (список камней окна g_nearStones), и камень выше воды — препятствие:
//     глубина сквозь него — доля g_nearPassage глубины без камня (проводимость грани и трение по Маннингу на этой глубине —
//     вода обходит камень, а ряд камней на уступе без просвета не запружает ручей: уровень под «жёсткой крышкой» не
//     поднимается — вода сочится);
//   - удержание завихрённости (vorticity confinement, Fedkiw, Stam, Jensen 2001; mainNearVorticity, сила — в
//     mainFlowViscosity): полулагранжев перенос размывает вихри за камнем и на повороте, сила к центрам вихрей их держит;
//   - давление и скорость прошлого кадра — начальное приближение: окно идёт вслед за водой, шаги — по времени игры
////////////////////////////////////////////////////////////////////////////////

#include "water_simulation.sh"

DM_SRV( StructuredBuffer<uint2>, g_flowTiles, 5 );	// группы счёта (группа потоков — 8 × 8 ячеек): тайлы с озером и соседние;
													// FLOW_DETAIL — блоки плиток: срез, номер блока в плитке

#if defined( FLOW_NEAR )
DM_SRV( Texture2DArray<float4>, g_detailStatic, 3 );		// детальные плитки: уровень воды (−1e9 — нет), u₀ по X и Z мира, R русла
DM_SRV( Texture2DArray<float2>, g_terrainDetailTiles, 6 );	// детальная земля: нормированная высота, врез
DM_SRV( Texture2D<uint>, g_detailIndex, 7 );				// плитка (x, z) мира → срез + 1, 0 — плитки нет
DM_SRV( StructuredBuffer<float4>, g_nearStones, 9 );		// камни русла в окне: центр основания (x, y, z), поперечник
DM_SRV( Texture2DArray<float4>, g_detailWater, 10 );		// детальная вода (SLOT_WATER_DETAIL): уровень, течение X, Z, глубина
#define FLOW_TEXTURE( type ) RWTexture2D<type>
#elif defined( FLOW_DETAIL )
DM_SRV( Texture2DArray<float4>, g_detailStatic, 3 );		// уровень воды (−1e9 — нет), u₀ по X и Z мира, R русла (0 — озеро)
DM_SRV( Texture2DArray<float2>, g_terrainDetailTiles, 6 );	// детальная земля: нормированная высота, врез
DM_SRV( Texture2D<uint>, g_detailIndex, 7 );				// плитка (x, z) мира → срез + 1, 0 — плитки нет
DM_SRV( StructuredBuffer<uint2>, g_detailTileCoords, 8 );	// срез → плитка (x, z) мира
DM_SRV( Texture2DArray<float>, g_detailStones, 9 );			// верх камня русла над ячейкой, м (−1e9 — камня нет)
DM_SRV( Texture2D<float4>, g_coarseWater, 10 );				// вода сетки 1 м (SLOT_WATER): глубина, течение X, Z мира
#define FLOW_TEXTURE( type ) RWTexture2DArray<type>
#else
DM_SRV( Texture2D<float4>, g_staticWater, 3 );	// уровень ручья (x), течение по X и Z мира (y, z), уровень озера (w)
DM_SRV( Texture2D<float2>, g_stoneArea, 6 );	// камни русла: площадь поперёк потока на м² дна (погружённая часть) — своя и средняя вокруг
#define FLOW_TEXTURE( type ) RWTexture2D<type>
#endif

DM_UAV( FLOW_TEXTURE( float4 ), g_info, 0 );		// x, y — скорость ручья по Маннингу u₀ (оси текселей), z — вид ячейки, w — глубина текущего слоя
DM_UAV( FLOW_TEXTURE( float2 ), g_velocity, 1 );	// скорость на восточной (x) и южной (y) грани, м/с (до давления — после вязкости и трения)
DM_UAV( FLOW_TEXTURE( float2 ), g_advected, 2 );	// после переноса
DM_UAV( FLOW_TEXTURE( float ), g_pressure, 3 );		// давление (м²/с: u −= ∇p)
DM_UAV( FLOW_TEXTURE( float ), g_divergence, 4 );	// ∇·(h (u − u₀)), м/с
DM_UAV( FLOW_TEXTURE( float4 ), g_output, 5 );		// для шейдеров (SLOT_WATER; FLOW_DETAIL — детальная вода): скорость — в y, z
#if defined( FLOW_NEAR )
DM_UAV( RWTexture2D<float2>, g_info2, 6 );			// x — R русла (> 0 — ручей), y — глубина без камней (текущий слой)
DM_UAV( RWTexture2D<float>, g_vorticity, 7 );		// завихрённость в центре ячейки, 1/с (удержание завихрённости)
#elif defined( FLOW_DETAIL )
DM_UAV( RWTexture2DArray<float2>, g_info2, 6 );		// x — R русла (> 0 — ручей), y — глубина без камней (текущий слой)
DM_SRV( Texture2DArray<float2>, g_cachedFlow, 11 );	// mainFlowRestore: течение X, Z мира
#else
DM_SRV( Texture2D<float2>, g_cachedFlow, 11 );
#endif

#include "water_flow_constants.h"

static const float kindDry = 0.0f;
static const float kindLake = 1.0f;
static const float kindStream = 2.0f;
static const float kindOutside = 3.0f;	// FLOW_DETAIL: ячейка вне плиток, вода там — по сетке 1 м
static const int2 axes[2] = { int2( 1, 0 ), int2( 0, 1 ) };

#if defined( FLOW_DETAIL ) || defined( FLOW_NEAR )
#define FLOW_NESTED		// вложенная сетка: ячейки вне — с течением внешней сетки, крупный масштаб тянется к ней
#endif

#if defined( FLOW_NEAR )
#include "water_detail_flow_constants.h"
#include "water_near_flow_constants.h"

static const float zSign = 1.0f;			// ось y окна — по Z мира
static const float nearGravity = 9.81f;		// м/с²: давление → подъём поверхности (nearSurfaceLift)
static const float nearSurfaceReach = 0.5f;	// м: подъём поверхности — давление минус среднее в этом радиусе
static const float nearSurfaceDepthShare = 0.5f;	// подъём и просадка поверхности — не больше этой доли глубины ячейки

typedef uint2 CellAddress;
#define FLOW_CELL g_nearStep

// Ячейка мира (шаг g_nearStep от угла мира) → тексель окна по модулю его стороны: окно сдвигается, а ячейка остаётся в
// том же текселе; false — ячейка вне окна
bool cellAddress( int2 cell, out CellAddress address )
{
	const int size = (int)g_nearSize;
	address = uint2( ( cell % size + size ) % size );
	const int2 local = cell - g_nearOrigin;
	return all( local >= 0 ) && all( local < size );
}

// Тексель детальной плитки под точкой мира, м: x, y — дробные в текселях плитки (с полем в тексель), z — срез; false —
// плитки нет
bool detailTexel( float2 world, out float3 texel )
{
	texel = 0.0f;
	const float2 grid = world / g_detailStep;
	const int2 tile = int2( floor( grid / (float)g_detailCells ) );
	if( any( tile < 0 ) || any( tile >= (int)g_detailTilesPerSide ) )
		return false;
	const uint slice = g_detailIndex.Load( int3( tile, 0 ) );
	texel = float3( grid - float2( tile ) * (float)g_detailCells + 1.0f, (float)slice - 1.0f );
	return slice != 0;
}

// Четыре текселя вокруг точки (дробный тексель) и билинейные веса
float bilinearWeight( float2 t, int2 offset )
{
	return ( offset.x ? t.x : 1.0f - t.x ) * ( offset.y ? t.y : 1.0f - t.y );
}

// Статичная вода плиток в точке мира — билинейно по текселям с водой: уровень, u₀ по X и Z мира, R; false — воды нет
bool detailStaticAt( float2 world, out float4 value )
{
	value = 0.0f;
	float3 texel;
	if( !detailTexel( world, texel ) )
		return false;
	const float2 grid = texel.xy - 0.5f;
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float4 sum = 0.0f;
	float weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		const float weight = bilinearWeight( t, offset );
		const float4 sample = g_detailStatic.Load( int4( c + offset, texel.z, 0 ) );
		if( weight <= 0.0f || sample.x <= -1e8f )
			continue;
		sum += weight * sample;
		weights += weight;
	}
	if( weights <= 1e-6f )
		return false;
	value = sum / weights;
	return true;
}

// Детальная вода в точке мира — билинейно по мокрым текселям: x — уровень, y, z — течение по X и Z мира, w — глубина;
// false — воды нет
bool detailWaterAt( float2 world, out float4 value )
{
	value = 0.0f;
	float3 texel;
	if( !detailTexel( world, texel ) )
		return false;
	const float2 grid = texel.xy - 0.5f;
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float4 sum = 0.0f;
	float weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		const float weight = bilinearWeight( t, offset );
		const float4 sample = g_detailWater.Load( int4( c + offset, texel.z, 0 ) );
		if( weight <= 0.0f || sample.w < g_detailWetDepth )
			continue;
		sum += weight * sample;
		weights += weight;
	}
	if( weights <= 1e-6f )
		return false;
	value = sum / weights;
	return true;
}

// Детальная земля в точке мира, м (билинейно); −1e9 — плитки нет
float detailBed( float2 world )
{
	float3 texel;
	if( !detailTexel( world, texel ) )
		return -1e9f;
	const float2 grid = texel.xy - 0.5f;
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float height = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		height += bilinearWeight( t, offset ) * g_terrainDetailTiles.Load( int4( c + offset, texel.z, 0 ) ).x;
	}
	return height * g_heightMultiplier + g_heightOffset;
}

// Течение внешней (детальной) сетки в точке p (ячейки окна от угла мира) — по осям окна (они — X и Z мира)
float2 coarseVelocity( float2 p )
{
	float4 water;
	return detailWaterAt( p * g_nearStep, water ) ? water.yz : 0.0f;
}

// Глубина детальной воды у ячейки
float coarseDepth( int2 cell )
{
	float4 water;
	return detailWaterAt( ( cell + 0.5f ) * g_nearStep, water ) ? water.w : 0.0f;
}

// Доля шага, на которую крупный масштаб тянется к внешней сетке
float nudgeShare()
{
	return g_nearNudgeShare;
}
#elif defined( FLOW_DETAIL )
#include "water_detail_flow_constants.h"

static const float stoneDepthShare = 0.5f;		// камень отнимает у глубины не больше этой доли
static const float nudgeTime = 2.0f;			// с: средняя блока тянется к решению 1 м за это время
static const float zSign = 1.0f;			// ось y сетки — по Z мира

typedef uint3 CellAddress;
#define FLOW_CELL g_detailStep

// Ячейка мира → тексель среза её плитки (с полем); false — плитки нет
bool cellAddress( int2 cell, out CellAddress address )
{
	address = 0;
	if( any( cell < 0 ) )
		return false;
	const uint2 tile = uint2( cell ) / g_detailCells;
	if( any( tile >= g_detailTilesPerSide ) )
		return false;
	const uint slice = g_detailIndex.Load( int3( tile, 0 ) );
	address = uint3( uint2( cell ) - tile * g_detailCells + 1, slice - 1 );
	return slice != 0;
}

// Течение решения 1 м в точке p (ячейки детальной сетки от угла мира) — по осям детальной сетки: билинейно по мокрым
// клеткам (веса перенормируются; у сухой клетки течения нет, и у берега узкого ручья среднее с ней вышло бы вдвое меньше)
float2 coarseVelocity( float2 p )
{
	const float2 world = p * g_detailStep;
	const float2 grid = float2( world.x, g_size * g_cellSize - world.y ) / g_cellSize - 0.5f;
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float2 sum = 0.0f;
	float weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		const int2 texel = c + offset;
		const float weight = ( offset.x ? t.x : 1.0f - t.x ) * ( offset.y ? t.y : 1.0f - t.y );
		if( weight <= 0.0f || !inside( texel ) )
			continue;
		const float4 water = g_coarseWater.Load( int3( texel, 0 ) );
		if( water.x < g_detailWetDepth )
			continue;
		sum += weight * water.yz;
		weights += weight;
	}
	return weights > 1e-6f ? sum / weights : 0.0f;
}

// Глубина воды сетки 1 м у ячейки
float coarseDepth( int2 cell )
{
	const float2 world = ( cell + 0.5f ) * g_detailStep;
	const int2 coarse = int2( floor( world.x / g_cellSize ), (int)g_size - 1 - (int)floor( world.y / g_cellSize ) );
	return inside( coarse ) ? g_coarseWater.Load( int3( coarse, 0 ) ).x : 0.0f;
}

// Доля шага, на которую крупный масштаб тянется к внешней сетке
float nudgeShare()
{
	return min( g_flowTimeStep / nudgeTime, 1.0f );
}
#else
static const float zSign = -1.0f;			// строки сетки — против Z мира

typedef int2 CellAddress;
#define FLOW_CELL g_cellSize

bool cellAddress( int2 cell, out CellAddress address )
{
	address = cell;
	return inside( cell );
}

float4 cellInfo( int2 cell )
{
	return inside( cell ) ? g_info[cell] : 0.0f;
}
#endif

#ifdef FLOW_NESTED
// Вид, u₀ и глубина ячейки (info) и R с глубиной без камней (extra); вне своей сетки — по внешней (вид «вне»)
void loadCell( int2 cell, out float4 info, out float2 extra )
{
	CellAddress address;
	if( cellAddress( cell, address ) )
	{
		info = g_info[address];
		extra = g_info2[address];
		return;
	}
	const float depth = coarseDepth( cell );
	info = depth >= g_detailWetDepth ? float4( 0.0f, 0.0f, kindOutside, min( depth, g_flowLayer ) ) : 0.0f;
	extra = float2( 0.0f, info.w );
}

float4 cellInfo( int2 cell )
{
	float4 info;
	float2 extra;
	loadCell( cell, info, extra );
	return info;
}
#endif

// Скорости граней, которые хранит ячейка (восточной и южной); вложенная сетка: у ячейки вне — течение внешней
float2 faceVelocity( int2 cell )
{
	CellAddress address;
	if( cellAddress( cell, address ) )
		return g_velocity[address];
#ifdef FLOW_NESTED
	return cellInfo( cell ).z == kindOutside ? coarseVelocity( cell + 0.5f ) : 0.0f;
#else
	return 0.0f;
#endif
}

// Ячейка счёта: группа — тайл (блок плитки), поток — ячейка. Вне групп текстуры счёта — нули (сухо)
bool flowCell( uint3 group, uint3 thread, out int2 cell, out CellAddress address )
{
	cell = 0;
	address = 0;
	if( group.x >= g_flowTileCount )
		return false;
#if defined( FLOW_NEAR )
	// Группа — блок 8 × 8 окна по строкам блоков
	const uint blocks = g_nearSize / 8;
	cell = g_nearOrigin + int2( uint2( group.x % blocks, group.x / blocks ) * 8 + thread.xy );
	return cellAddress( cell, address );
#elif defined( FLOW_DETAIL )
	const uint2 block = g_flowTiles[group.x];
	const uint blocksPerSide = g_detailCells / 8;
	const uint2 tile = g_detailTileCoords[block.x];
	const uint2 local = uint2( block.y % blocksPerSide, block.y / blocksPerSide ) * 8 + thread.xy;
	cell = int2( tile * g_detailCells + local );
	address = uint3( local + 1, block.x );
	return true;
#else
	cell = int2( g_flowTiles[group.x] * 8 + thread.xy );
	address = cell;
	return inside( cell );
#endif
}

#ifdef FLOW_NEAR
// Пропускная доля ячейки: у камня выше воды (глубина упёрлась в долю g_nearPassage глубины без камня) — g_nearPassage, иначе 1
float obstacleShare( float4 info, float2 extra )
{
	return extra.y > 0.0f && info.w <= g_nearPassage * extra.y * 1.01f + 1e-5f ? g_nearPassage : 1.0f;
}
#endif

// Грань между ячейкой a и соседом a + axes[axis]
struct Face
{
	uint   state;		// закрыта (берег), свободна (между двумя мокрыми) или задана (FLOW_DETAIL: к ячейке вне плиток)
	float  depth;		// глубина на грани
	float2 base;		// скорость ручья по Маннингу u₀ у грани (оси текселей): два ручья — средняя, ручей и озеро — ручья, озёра — 0
	// Глубина без камней — расход сети ∇·(h₀ u₀) и сила тяжести ручья (у сетки 1 м — глубина грани); 0 — грань закрыта
	float  baseDepth;
	bool   stream;		// у ручья (хотя бы с одной стороны) — трение по n русел, у озера — своё
	// Пропускная доля грани (окно у камеры: у камня выше воды — g_nearPassage, иначе 1): ею умножаются проводимость давления
	// и поправка давлением. Под «жёсткой крышкой» поправка разгоняет воду на мелкой грани так же, как на глубокой, и одна
	// малая глубина воду сквозь камень не останавливала (в камне течение было быстрее, чем рядом); так камень — пористое
	// препятствие (как штраф Бринкмана): вода обходит его, а без обхода сочится
	float  open;
};

static const uint faceClosed = 0;
static const uint faceFree = 1;
static const uint faceFixed = 2;

Face face( int2 a, uint axis )
{
	Face result;
	result.state = faceClosed;
	result.depth = 0.0f;
	result.base = 0.0f;
	result.baseDepth = 0.0f;
	result.stream = false;
	result.open = 1.0f;
	const int2 b = a + axes[axis];
#ifdef FLOW_NESTED
	float4 infoA, infoB;
	float2 extraA, extraB;
	loadCell( a, infoA, extraA );
	loadCell( b, infoB, extraB );
	if( infoA.z == kindDry || infoB.z == kindDry || ( infoA.z == kindOutside && infoB.z == kindOutside ) )
		return result;
	// Ручей — с u₀ и R
	const bool streamA = extraA.x > 0.0f;
	const bool streamB = extraB.x > 0.0f;
	result.baseDepth = 0.5f * ( extraA.y + extraB.y );
	result.state = infoA.z == kindOutside || infoB.z == kindOutside ? faceFixed : faceFree;
	result.depth = 0.5f * ( infoA.w + infoB.w );
#else
	const float4 infoA = cellInfo( a );
	const float4 infoB = cellInfo( b );
	if( infoA.z == kindDry || infoB.z == kindDry )
		return result;
	const bool streamA = infoA.z == kindStream;
	const bool streamB = infoB.z == kindStream;
	result.state = faceFree;
	result.depth = 0.5f * ( infoA.w + infoB.w );
	result.baseDepth = result.depth;
#endif
	result.stream = streamA || streamB;
#ifdef FLOW_NEAR
	result.open = min( obstacleShare( infoA, extraA ), obstacleShare( infoB, extraB ) );
#endif
	result.base = streamA && streamB ? 0.5f * ( infoA.xy + infoB.xy ) : streamA ? infoA.xy : streamB ? infoB.xy : 0.0f;
	return result;
}

// Заданная скорость грани (FLOW_DETAIL: к ячейке вне плиток) — течение сетки 1 м в точке грани
float fixedFaceVelocity( int2 cell, uint axis )
{
#ifdef FLOW_NESTED
	return coarseVelocity( cell + ( axis == 0 ? float2( 1.0f, 0.5f ) : float2( 0.5f, 1.0f ) ) )[axis];
#else
	return 0.0f;
#endif
}

float pressureAt( int2 cell )
{
	CellAddress address;
	return cellAddress( cell, address ) ? g_pressure[address] : 0.0f;
}

// Перенесённые скорости граней ячейки; FLOW_DETAIL: у ячейки вне плиток — течение сетки 1 м
float2 advectedVelocity( int2 cell )
{
	CellAddress address;
	if( cellAddress( cell, address ) )
		return g_advected[address];
#ifdef FLOW_NESTED
	return cellInfo( cell ).z == kindOutside ? coarseVelocity( cell + 0.5f ) : 0.0f;
#else
	return 0.0f;
#endif
}

float velocityAt( int2 cell, uint axis )
{
	CellAddress address;
	return cellAddress( cell, address ) ? g_velocity[address][axis] : 0.0f;
}

// Ячейки и грани: так же, как mainStatic (water_simulation.cs) делит одно поле на озеро и ручей
[numthreads( 8, 8, 1 )]
void mainFlowInit( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
#if defined( FLOW_NEAR )
	// Ячейка окна — из детальных плиток билинейно: уровень воды, u₀ и R, дно — детальная земля; камни русла — полуэллипсоиды
	// на сетке окна. Камень выше воды — препятствие: глубина сквозь него — g_nearPassage глубины без камня. Каждый кадр (окно
	// сдвигается), давление и скорость остаются — начальное приближение
	const float2 world = ( cell + 0.5f ) * g_nearStep;
	float4 water;
	const bool wet = detailStaticAt( world, water );
	const float bed = detailBed( world );
	float top = bed;
	[loop] for( uint s = 0; s < g_nearStoneCount; ++s )
	{
		const float4 stone = g_nearStones[s];
		const float radius = 0.5f * stone.w;
		const float2 offset = world - stone.xz;
		const float share = dot( offset, offset ) / max( radius * radius, 1e-6f );
		if( share < 1.0f )
			top = max( top, stone.y + g_nearStoneHeight * stone.w * sqrt( 1.0f - share ) );
	}
	const bool stream = water.w > 0.0f;
	const float baseDepth = wet && bed > -1e8f ? max( water.x - bed, 0.0f ) : 0.0f;
	const float depth = baseDepth > 0.0f ? max( water.x - top, g_nearPassage * baseDepth ) : 0.0f;
	float kind = kindDry;
	if( stream && depth > 0.0f )
		kind = kindStream;
	else if( !stream && depth >= g_flowMinDepth )
		kind = kindLake;
	g_info[address] = float4( kind == kindStream ? water.yz : 0.0f, kind, min( depth, g_flowLayer ) );
	g_info2[address] = float2( kind == kindStream ? water.w : 0.0f, kind != kindDry ? min( baseDepth, g_flowLayer ) : 0.0f );
	return;
#elif defined( FLOW_DETAIL )
	// Дно — детальная земля, над ним камень русла (отнимает не больше stoneDepthShare глубины); ручей — где у воды есть R
	const float4 water = g_detailStatic.Load( int4( address, 0 ) );
	const float bed = g_terrainDetailTiles.Load( int4( address, 0 ) ).x * g_heightMultiplier + g_heightOffset;
	const float top = max( bed, g_detailStones.Load( int4( address, 0 ) ) );
	const bool wet = water.x > -1e8f;
	const bool stream = water.w > 0.0f;
	const float baseDepth = wet ? max( water.x - bed, 0.0f ) : 0.0f;
	const float depth = baseDepth > 0.0f ? max( water.x - top, ( 1.0f - stoneDepthShare ) * baseDepth ) : 0.0f;
	float kind = kindDry;
	if( stream && depth > 0.0f )
		kind = kindStream;
	else if( !stream && depth >= g_flowMinDepth )
		kind = kindLake;
	g_info[address] = float4( kind == kindStream ? water.yz : 0.0f, kind, min( depth, g_flowLayer ) );
	g_info2[address] = float2( kind == kindStream ? water.w : 0.0f, kind != kindDry ? min( baseDepth, g_flowLayer ) : 0.0f );
#else
	const float4 stream = g_staticWater.Load( int3( cell, 0 ) );
	const float ground = terrain( cell );
	const float lake = stream.w > -1e8f ? max( stream.w - ground, 0.0f ) : 0.0f;
	const float streamDepth = stream.x > -1e8f ? max( stream.x - ground, 0.0f ) : 0.0f;
	const float depth = max( lake, streamDepth );
	float kind = kindDry;
	float2 fixedVelocity = 0.0f;
	if( streamDepth > lake && streamDepth > 0.0f )
	{
		kind = kindStream;
		fixedVelocity = float2( stream.y, -stream.z );
	}
	else if( depth >= g_flowMinDepth )
		kind = kindLake;
	// Течёт слой у поверхности: ручей растекается по озеру поверху, а не всей толщей
	g_info[address] = float4( fixedVelocity, kind, min( depth, g_flowLayer ) );
#endif
	g_pressure[address] = 0.0f;
	g_divergence[address] = 0.0f;
}

// Начальная скорость граней (после mainFlowInit: грань читает обе ячейки) — у сетки 1 м u₀ (у озёр 0): расчёт идёт от сети
// ручьёв; у детальной — решение 1 м, у заданной грани оно же и остаётся. Окно у камеры — только у ячеек, вошедших в него в
// этом кадре (не было в окне прошлого кадра, g_nearReset — все): течение детальной сетки, давление и завихрённость — нули
[numthreads( 8, 8, 1 )]
void mainFlowFaces( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
#if defined( FLOW_NEAR )
	const int2 previous = cell - g_nearPreviousOrigin;
	if( g_nearReset == 0 && all( previous >= 0 ) && all( previous < (int)g_nearSize ) )
		return;
	g_pressure[address] = 0.0f;
	g_divergence[address] = 0.0f;
	g_vorticity[address] = 0.0f;
#endif
	float2 velocity;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
#ifdef FLOW_NESTED
		velocity[axis] = f.state != faceClosed ? fixedFaceVelocity( cell, axis ) : 0.0f;
#else
		velocity[axis] = f.state == faceFree ? f.base[axis] : 0.0f;
#endif
	}
	g_velocity[address] = velocity;
	g_advected[address] = velocity;
}

// Компонента axis скорости в точке p (ячейки, от угла сетки) — билинейно по её граням: грань текселя i по оси —
// в i + 1, по другой оси — в i + ½. Только открытые грани (веса перенормируются): закрытая грань у берега хранит 0, и в
// узком русле выборка с ней каждый шаг отнимала бы у течения до четверти скорости — «прилипание» к сухому. fallback —
// если открытых нет
float sampleComponent( float2 p, uint axis, float fallback )
{
	const float2 grid = p - ( axis == 0 ? float2( 1.0f, 0.5f ) : float2( 0.5f, 1.0f ) );
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float sum = 0.0f, weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		const int2 texel = c + offset;
		const float weight = ( offset.x ? t.x : 1.0f - t.x ) * ( offset.y ? t.y : 1.0f - t.y );
		if( weight <= 0.0f || face( texel, axis ).state != faceFree )
			continue;
		sum += weight * faceVelocity( texel )[axis];
		weights += weight;
	}
	return weights > 1e-6f ? sum / weights : fallback;
}

// Скорость в точке грани: своя компонента — с грани, другая — средняя четырёх соседних граней другой оси; advected —
// по перенесённому полю, иначе — по полю шага
float2 velocityAtFace( int2 cell, float own, uint axis, bool advected )
{
	const uint other = 1 - axis;
	const int2 step = axes[axis];
	const int2 back = -axes[other];
	const int2 cells[4] = { cell, cell + step, cell + back, cell + step + back };
	float across = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
		across += 0.25f * ( advected ? advectedVelocity( cells[k] ) : faceVelocity( cells[k] ) )[other];
	return axis == 0 ? float2( own, across ) : float2( across, own );
}

[numthreads( 8, 8, 1 )]
void mainFlowAdvect( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	float2 result = g_velocity[address];
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
		if( f.state != faceFree )
			continue;
		const float2 position = cell + ( axis == 0 ? float2( 1.0f, 0.5f ) : float2( 0.5f, 1.0f ) );
		const float own = result[axis];
		const float2 velocity = velocityAtFace( cell, own, axis, false );
		// Назад по течению на шаг (полулагранжев перенос: устойчив при любом шаге)
		result[axis] = sampleComponent( position - velocity * g_flowTimeStep / FLOW_CELL, axis, own );
	}
	g_advected[address] = result;
}

#ifdef FLOW_NEAR
// Завихрённость в центре ячейки по перенесённому полю (mainNearVorticity) — для удержания завихрённости в mainFlowViscosity
float vorticityAt( int2 cell )
{
	CellAddress address;
	return cellAddress( cell, address ) ? g_vorticity[address] : 0.0f;
}

// Скорость в центре ячейки — средняя двух граней по каждой оси (перенесённое поле)
float2 centreVelocity( int2 cell )
{
	return 0.5f * float2( advectedVelocity( cell ).x + advectedVelocity( cell - axes[0] ).x,
						  advectedVelocity( cell ).y + advectedVelocity( cell - axes[1] ).y );
}

// Сила удержания завихрённости на грани cell по оси axis, м/с²: ε · h · (N × ω), N — направление роста |ω| (Fedkiw, Stam,
// Jensen 2001, «Visual Simulation of Smoke»): подталкивает поток вокруг центров вихрей, которые перенос размывает
float eddyForce( int2 cell, uint axis )
{
	if( g_nearEddies <= 0.0f )
		return 0.0f;
	const int2 next = cell + axes[axis];
	const int2 across = axes[1 - axis];
	const float omega = 0.5f * ( vorticityAt( cell ) + vorticityAt( next ) );
	if( abs( omega ) < 1e-4f )
		return 0.0f;
	// Рост |ω| на грани: вдоль оси — между ячейками, поперёк — по соседям обеих
	const float along = abs( vorticityAt( next ) ) - abs( vorticityAt( cell ) );
	const float sideways = 0.25f * ( abs( vorticityAt( cell + across ) ) + abs( vorticityAt( next + across ) ) -
									 abs( vorticityAt( cell - across ) ) - abs( vorticityAt( next - across ) ) );
	float2 gradient = axis == 0 ? float2( along, sideways ) : float2( sideways, along );
	const float length2 = dot( gradient, gradient );
	if( length2 < 1e-10f )
		return 0.0f;
	gradient *= rsqrt( length2 );
	// N × (ω ẑ) = (N.y ω, −N.x ω)
	const float2 force = g_nearEddies * g_nearStep * omega * float2( gradient.y, -gradient.x );
	return force[axis];
}
#endif

[numthreads( 8, 8, 1 )]
void mainFlowViscosity( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	float2 result = g_advected[address];
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
		if( f.state != faceFree )
			continue;
		const float own = result[axis];
		const float2 velocity = velocityAtFace( cell, own, axis, true );
		// Вихревая вязкость — перемешивание: струя расширяется и теряет скорость. По открытым соседним граням (свободное
		// скольжение у берега): тормозит дно (трение ниже), а прилипание к стенке на сетке 1 м гасило бы узкий ручей целиком
		float spread = 0.0f;
		[unroll] for( uint k = 0; k < 4; ++k )
		{
			const int2 neighbor = cell + ( k < 2 ? int2( k == 0 ? 1 : -1, 0 ) : int2( 0, k == 2 ? 1 : -1 ) );
			if( face( neighbor, axis ).state == faceFree )
				spread += advectedVelocity( neighbor )[axis] - own;
		}
		float value = own + g_flowViscosity * spread;
		// Трение дна по Маннингу, неявно: мелкая вода у берега и над камнем тормозит сильнее. Сила тяжести вдоль уклона
		// ручья — то, что трение отнимает у скорости u₀ на глубине без камней: у сетки 1 м это глубина грани — тогда u₀
		// равновесие (на ровном участке поле — по Маннингу), отклонения от него (инерция на изгибе и слиянии, обход
		// препятствия) гаснут за время трения; у детальной — равновесие u₀ (h / h₀)^(2/3): над камнем медленнее (профиль
		// поперёк русла — уже в u₀)
		// Трение ручья — по n русел, по которому считана u₀: на равновесие n не влияет (сила тяжести возмещает трение), но
		// задаёт, за какое время поле к нему приходит. С трением озёр (вдвое меньше n — вчетверо дольше, ~12 с у ручья) профиль
		// поперёк русла не успевал сложиться: перенос и давление уносили и выравнивали его, а вода у камней и изгибов
		// закручивалась назад
		const float friction = f.stream ? g_flowStreamFriction : g_flowFriction;
		const float drag = friction / pow( max( f.depth, g_flowMinDepth ), 4.0f / 3.0f );
		const float forcing = friction / pow( max( f.baseDepth, g_flowMinDepth ), 4.0f / 3.0f );
#ifdef FLOW_NEAR
		// Удержание завихрённости: вихри за камнем и на повороте не размываются переносом
		value += eddyForce( cell, axis ) * g_flowTimeStep;
#endif
		const float speed = length( axis == 0 ? float2( value, velocity.y ) : float2( velocity.x, value ) );
#ifdef FLOW_NESTED
		// Камни — геометрия детальной сетки (дно и препятствия), сопротивление сетки 1 м не нужно. Окно у камеры: в грани
		// камня выше воды и сила тяжести, и скорость — × пропускная доля (штраф Бринкмана: вода в камне почти стоит)
		value = ( value + f.open * forcing * length( f.base ) * f.base[axis] ) / ( 1.0f + drag * speed ) * f.open;
#else
		// Камни русла — сопротивление формы: Cd / 2 · a / h · |u| u (a — площадь камней поперёк потока на м² дна у грани).
		// Шероховатость дна по Маннингу уже держит камни участка в среднем, поэтому сила тяжести возмещает среднее
		// сопротивление камней вокруг: у камня течение медленнее u₀, между камнями — быстрее, средняя по участку — u₀
		const int2 next = cell + axes[axis];
		const float2 area = 0.5f * ( g_stoneArea[cell] + ( inside( next ) ? g_stoneArea[next] : 0.0f ) );
		const float stones = g_flowStoneDrag * area.x / max( f.depth, g_flowMinDepth );
		const float stonesAround = g_flowStoneDrag * area.y / max( f.depth, g_flowMinDepth );
		value = ( value + ( forcing + stonesAround ) * length( f.base ) * f.base[axis] ) / ( 1.0f + ( drag + stones ) * speed );
#endif
		result[axis] = value;
	}
	g_velocity[address] = result;
}

#ifdef FLOW_NESTED
groupshared float4 g_nudgeSums[64];

// Крупный масштаб — к внешней сетке (после mainFlowViscosity, до давления): по свободным граням блока (группа потоков —
// блок 8 × 8) средняя по глубине разница скорости внешней сетки (у детальной — 1 м, у окна — детальной) и своей, доля шага
// (nudgeShare) — всем граням блока
[numthreads( 8, 8, 1 )]
void mainFlowNudge( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint index : SV_GroupIndex )
{
	int2 cell;
	CellAddress address;
	const bool valid = flowCell( group, thread, cell, address );
	float2 velocity = 0.0f;
	bool open[2] = { false, false };
	float4 sums = 0.0f;
	if( valid )
	{
		velocity = g_velocity[address];
		[unroll] for( uint axis = 0; axis < 2; ++axis )
		{
			const Face f = face( cell, axis );
			open[axis] = f.state == faceFree;
			if( !open[axis] )
				continue;
			sums[axis * 2] = f.depth * ( fixedFaceVelocity( cell, axis ) - velocity[axis] );
			sums[axis * 2 + 1] = f.depth;
		}
	}
	g_nudgeSums[index] = sums;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for( uint width = 32; width > 0; width >>= 1 )
	{
		if( index < width )
			g_nudgeSums[index] += g_nudgeSums[index + width];
		GroupMemoryBarrierWithGroupSync();
	}
	if( !valid )
		return;
	const float4 total = g_nudgeSums[0];
	const float share = nudgeShare();
	[unroll] for( uint axis = 0; axis < 2; ++axis )
		if( open[axis] && total[axis * 2 + 1] > 1e-6f )
			velocity[axis] += share * total[axis * 2] / total[axis * 2 + 1];
	g_velocity[address] = velocity;
}
#endif

// Расход ячейки наружу через грань: тексель грани, ось и знак (восток и юг — свои грани, запад и север — соседей)
struct CellFace
{
	int2 texel;
	uint axis;
	float sign;
	int2 neighbor;
};

CellFace cellFace( int2 cell, uint i )
{
	CellFace result;
	result.axis = i & 1;
	const bool own = i < 2;
	result.texel = own ? cell : cell - axes[result.axis];
	result.sign = own ? 1.0f : -1.0f;
	result.neighbor = own ? cell + axes[result.axis] : cell - axes[result.axis];
	return result;
}

// Ячейка, у которой считаются расхождение и давление
bool solved( float kind )
{
	return kind == kindLake || kind == kindStream;
}

// Расхождение: у ручья — отклонения расхода от сети ручьёв ∇·(h u − h₀ u₀) (сколько воды прибывает и уходит, задаёт u₀,
// с гранью к озеру: вода впадения уходит в озеро, истока — приходит из него; у края карты грань закрыта, и вода ручья там
// уходит; у камня расход сети идёт сквозь него, течение — в обход); у озера — расхода ∇·(h u): вода ручья входит в озеро
// и проходит его до истока, а не пропадает у берега
[numthreads( 8, 8, 1 )]
void mainFlowDivergence( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	const float kind = g_info[address].z;
	if( !solved( kind ) )
		return;
	const float base = kind == kindStream ? 1.0f : 0.0f;
	float divergence = 0.0f;
	[unroll] for( uint i = 0; i < 4; ++i )
	{
		const CellFace cf = cellFace( cell, i );
		const Face f = face( cf.texel, cf.axis );
		float flux = 0.0f;
		if( f.state == faceFree )
			flux = f.depth * velocityAt( cf.texel, cf.axis );
		else if( f.state == faceFixed )
			flux = f.depth * fixedFaceVelocity( cf.texel, cf.axis );
		divergence += cf.sign * ( flux - base * f.baseDepth * f.base[cf.axis] );
	}
	g_divergence[address] = divergence / FLOW_CELL;
}

// ∑ H (p_соседа − p) / l² − ε p = расхождение (mainFlowDivergence) по свободным граням
void relaxPressure( uint3 group, uint3 thread, uint parity )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) || ( ( cell.x + cell.y ) & 1 ) != parity || !solved( g_info[address].z ) )
		return;
	float weights = g_flowScreen;
	float sum = -g_divergence[address] * FLOW_CELL * FLOW_CELL;
	[unroll] for( uint i = 0; i < 4; ++i )
	{
		const CellFace cf = cellFace( cell, i );
		const Face f = face( cf.texel, cf.axis );
		if( f.state != faceFree )
			continue;
		weights += f.depth * f.open;
		sum += f.depth * f.open * pressureAt( cf.neighbor );
	}
	const float previous = g_pressure[address];
	g_pressure[address] = lerp( previous, sum / weights, g_flowRelaxation );
}

[numthreads( 8, 8, 1 )]
void mainFlowRed( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	relaxPressure( group, thread, 0 );
}

[numthreads( 8, 8, 1 )]
void mainFlowBlack( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	relaxPressure( group, thread, 1 );
}

// u = u* − ∇p по свободным граням (на месте: ячейка читает только свои грани и давление)
[numthreads( 8, 8, 1 )]
void mainFlowProject( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	float2 velocity = g_velocity[address];
	const float pressure = g_pressure[address];
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
		if( f.state != faceFree )
			continue;
		velocity[axis] -= f.open * ( pressureAt( cell + axes[axis] ) - pressure ) / FLOW_CELL;
	}
	g_velocity[address] = velocity;
}

#ifdef FLOW_NEAR
// Давление ячейки для подъёма поверхности: ячейка с водой, не камень выше воды (у него проводимость — доля Obstacle passage,
// и давление в нём скачет на метры)
bool surfacePressure( int2 cell, out float pressure )
{
	pressure = 0.0f;
	CellAddress at;
	if( !cellAddress( cell, at ) || !solved( g_info[at].z ) || g_info[at].w <= 2.0f * g_nearPassage * g_info2[at].y )
		return false;
	pressure = g_pressure[at];
	return true;
}

// Подъём поверхности воды в ячейке, м. Под «жёсткой крышкой» уровень неподвижен, и давление — это подъём, который разгонял
// бы воду: поправка u −= ∇p за шаг — −g·dt·∇η, то есть p = g·dt·η. Но крупный масштаб давления — не рельеф воды: вдоль
// ручья оно держит расход против силы тяжести и трения по Маннингу (на 10 м окна — перепад в метр), а уровень на этом
// масштабе уже даёт детальная вода. Поэтому подъём — давление у ячейки (среднее 3 × 3: у красно-чёрного Гаусса — Зейделя
// шахматная рябь) минус среднее в радиусе nearSurfaceReach: остаётся то, что размером с камень, — вспучивание перед
// камнем, просадка у его боков и над подводным, где вода быстрее. × g_nearSurface, не больше nearSurfaceDepthShare глубины
// ячейки (просадка не уходит под дно и камень, на мели у берега вода не поднимается над урезом); без шагов (время стоит) и у
// суши — 0
float nearSurfaceLift( int2 cell )
{
	CellAddress address;
	if( g_flowTimeStep <= 0.0f || g_nearSurface <= 0.0f || !cellAddress( cell, address ) || !solved( g_info[address].z ) )
		return 0.0f;
	float pressure;
	float local = 0.0f;
	float localCount = 0.0f;
	[unroll] for( int k = 0; k < 9; ++k )
		if( surfacePressure( cell + int2( k % 3 - 1, k / 3 - 1 ), pressure ) )
		{
			local += pressure;
			localCount += 1.0f;
		}
	if( localCount == 0.0f )
		return 0.0f;
	// Среднее вокруг — по сетке с шагом stride (не больше 9 × 9 ячеек)
	const int reach = clamp( (int)round( nearSurfaceReach / g_nearStep ), 2, 32 );
	const int stride = max( reach / 4, 1 );
	float around = 0.0f;
	float aroundCount = 0.0f;
	[loop] for( int j = -reach; j <= reach; j += stride )
		[loop] for( int i = -reach; i <= reach; i += stride )
			if( surfacePressure( cell + int2( i, j ), pressure ) )
			{
				around += pressure;
				aroundCount += 1.0f;
			}
	const float lift = ( local / localCount - around / max( aroundCount, 1.0f ) ) / ( nearGravity * g_flowTimeStep ) * g_nearSurface;
	const float limit = nearSurfaceDepthShare * g_info[address].w;
	return clamp( lift, -limit, limit );
}
#endif

// Скорость ячейки — средняя двух граней по каждой оси — в текстуру для шейдеров (оси мира), у озёр и ручьёв
[numthreads( 8, 8, 1 )]
void mainFlowOutput( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	const float kind = g_info[address].z;
#if defined( FLOW_NEAR )
	// Окно — все ячейки: у сухой — нули (иначе осталось бы прежнее с прошлого места окна)
	if( kind == kindDry )
	{
		g_output[address] = 0.0f;
		return;
	}
#elif defined( FLOW_DETAIL )
	if( kind == kindDry )
		return;
#else
	if( !solved( kind ) )
		return;
#endif
	// По открытым граням: у закрытой (берег) скорость 0, и у клетки вдоль берега среднее вышло бы вдвое меньше течения
	float2 velocity = 0.0f;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face east = face( cell, axis );
		const Face west = face( cell - axes[axis], axis );
		const bool eastOpen = east.state != faceClosed;
		const bool westOpen = west.state != faceClosed;
		const float open = ( eastOpen ? 1.0f : 0.0f ) + ( westOpen ? 1.0f : 0.0f );
		const float sum = ( eastOpen ? faceVelocity( cell )[axis] : 0.0f ) + ( westOpen ? faceVelocity( cell - axes[axis] )[axis] : 0.0f );
		velocity[axis] = open > 0.0f ? sum / open : 0.0f;
	}
	const float2 world = float2( velocity.x, zSign * velocity.y );
#if defined( FLOW_NEAR )
	// x — подъём поверхности по давлению (nearSurfaceLift), y, z — течение по X и Z мира, w — доля глубины над камнем (1 —
	// камня нет, у камня выше воды — Obstacle passage): где вода обходит камень
	const float surface = nearSurfaceLift( cell );
	const float baseDepth = g_info2[address].y;
	g_output[address] = float4( surface, world, baseDepth > 0.0f ? saturate( g_info[address].w / baseDepth ) : 0.0f );
#elif defined( FLOW_DETAIL )
	// Своя плитка и поле соседних (тексель поля — копия ячейки соседа: выборка у края плитки — без шва)
	const int2 tile = cell / (int)g_detailCells;
	[unroll] for( int k = 0; k < 9; ++k )
	{
		const int2 neighbor = tile + int2( k % 3 - 1, k / 3 - 1 );
		if( any( neighbor < 0 ) || any( neighbor >= (int)g_detailTilesPerSide ) )
			continue;
		const uint slice = g_detailIndex.Load( int3( neighbor, 0 ) );
		const int2 local = cell - neighbor * (int)g_detailCells;
		if( slice == 0 || any( local < -1 ) || any( local > (int)g_detailCells ) )
			continue;
		const uint3 texel = uint3( local + 1, slice - 1 );
		float4 state = g_output[texel];
		state.yz = world;
		g_output[texel] = state;
	}
#else
	float4 state = g_output[address];
	state.yz = world;
	g_output[address] = state;
#endif
}

// Скорость для шейдеров (y, z) — из кэша, у всех текселей: сетка 1 м — после mainStatic, детальная — после
// mainDetailSurface (тексели без расчёта держат там то же, что дал бы расчёт)
#ifndef FLOW_NEAR
[numthreads( 8, 8, 1 )]
void mainFlowRestore( uint3 id : SV_DispatchThreadID )
{
#ifdef FLOW_DETAIL
	uint width, height, slices;
	g_output.GetDimensions( width, height, slices );
	if( id.x >= width || id.y >= height || id.z >= slices )
		return;
	float4 state = g_output[id];
	state.yz = g_cachedFlow.Load( int4( id, 0 ) );
	g_output[id] = state;
#else
	if( any( id.xy >= g_size ) )
		return;
	float4 state = g_output[id.xy];
	state.yz = g_cachedFlow.Load( int3( id.xy, 0 ) );
	g_output[id.xy] = state;
#endif
}
#endif

#ifdef FLOW_NEAR
// ω = ∂v/∂x − ∂u/∂y в центре ячейки, где вода и у всех четырёх соседей (у берега сдвиг скорости — не вихрь, а стенка)
[numthreads( 8, 8, 1 )]
void mainNearVorticity( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	float omega = 0.0f;
	const bool wet = solved( cellInfo( cell ).z ) && solved( cellInfo( cell + axes[0] ).z ) && solved( cellInfo( cell - axes[0] ).z ) &&
					 solved( cellInfo( cell + axes[1] ).z ) && solved( cellInfo( cell - axes[1] ).z );
	if( wet )
		omega = ( centreVelocity( cell + axes[0] ).y - centreVelocity( cell - axes[0] ).y -
				  centreVelocity( cell + axes[1] ).x + centreVelocity( cell - axes[1] ).x ) / ( 2.0f * g_nearStep );
	g_vorticity[address] = omega;
}
#endif
