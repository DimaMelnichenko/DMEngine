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
// Ячейки: сухие (берег), озеро и ручей (обе считаются; у ручья — u₀). Грани: закрыта (берег) или свободна (между двумя
// мокрыми); у грани с ручьём — заданная скорость u₀ (два ручья — средняя, ручей и озеро — ручья). Класс WaterSimulation
// (solveFlow), docs/water.md, «Течение»
//
// FLOW_DETAIL (define) — то же течение на детальной сетке у русел (WaterSimulation::solveDetailFlow, docs/water.md,
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
//   - сила тяжести ручья — по гидравлическому радиусу русла R у ячейки (конвейер: тот же, по которому считана u₀), а не по
//     глубине грани: равновесие u₀ (h / R)^(2/3) — у оси быстрее, у берегов медленнее;
//   - крупный масштаб тянется к решению 1 м (mainFlowNudge, spectral nudging вложенных моделей, von Storch, Langenberg,
//     Feser 2000): средняя скорость блока 8 × 8 (2 м) по глубине — к средней сетки 1 м за nudgeTime, мелкий узор (обход
//     камней, профиль поперёк русла, струи) — свой. Давление под «жёсткой крышкой» поправляет расход только вблизи, и
//     камни подряд (запруда за запрудой, течение не успевает разогнаться) тормозили весь ручей втрое, а сетка 1 м камни
//     уже держит сопротивлением с возмещением (в среднем — по Маннингу)
////////////////////////////////////////////////////////////////////////////////

#include "water_simulation.sh"

DM_SRV( StructuredBuffer<uint2>, g_flowTiles, 5 );	// группы счёта (группа потоков — 8 × 8 ячеек): тайлы с озером и соседние;
													// FLOW_DETAIL — блоки плиток: срез, номер блока в плитке

#ifdef FLOW_DETAIL
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
#ifdef FLOW_DETAIL
DM_UAV( RWTexture2DArray<float2>, g_info2, 6 );		// x — R русла (0 — не ручей), y — глубина без камней (текущий слой)
#endif

// Раскладка — WaterSimulation::FlowParameters
cbuffer WaterFlowBuffer : register( b5 )
{
	uint  g_flowTileCount;		// групп в g_flowTiles
	uint3 g_flowTilesPadding;
	float g_flowTimeStep;		// с
	float g_flowViscosity;		// вихревая вязкость · dt / l² — доля, не больше 0,2 (устойчивость)
	float g_flowFriction;		// dt · g · n²: трение дна по Маннингу (и ручьёв, и озёр)
	float g_flowMinDepth;		// мельче — берег, м
	float g_flowScreen;			// экранирование давления, м: озеро без истока решается, вода «уходит в грунт»
	float g_flowRelaxation;		// верхняя релаксация давления (1 — Гаусс-Зейдель)
	float g_flowLayer;			// толщина текущего слоя, м: глубже течение не считается
	float g_flowStoneDrag;		// dt · Cd / 2: сопротивление камней русла (сетка 1 м)
};

static const float kindDry = 0.0f;
static const float kindLake = 1.0f;
static const float kindStream = 2.0f;
static const float kindOutside = 3.0f;	// FLOW_DETAIL: ячейка вне плиток, вода там — по сетке 1 м
static const int2 axes[2] = { int2( 1, 0 ), int2( 0, 1 ) };

#ifdef FLOW_DETAIL
// Раскладка — WaterSimulation::DetailFlowParameters
cbuffer WaterDetailFlowBuffer : register( b6 )
{
	uint  g_detailCells;			// ячеек плитки по стороне (без поля)
	uint  g_detailTilesPerSide;		// плиток по стороне мира
	float g_detailStep;				// сторона ячейки, м
	float g_detailWetDepth;			// ячейка вне плиток — мокрая, если по сетке 1 м глубже, м
};

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

// Вид, u₀ и глубина ячейки (info) и R с глубиной без камней (extra); вне плиток — по сетке 1 м
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

// Скорости граней, которые хранит ячейка (восточной и южной); FLOW_DETAIL: у ячейки вне плиток — течение сетки 1 м
float2 faceVelocity( int2 cell )
{
	CellAddress address;
	if( cellAddress( cell, address ) )
		return g_velocity[address];
#ifdef FLOW_DETAIL
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
#ifdef FLOW_DETAIL
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

// Грань между ячейкой a и соседом a + axes[axis]
struct Face
{
	uint   state;		// закрыта (берег), свободна (между двумя мокрыми) или задана (FLOW_DETAIL: к ячейке вне плиток)
	float  depth;		// глубина на грани
	float2 base;		// скорость ручья по Маннингу u₀ у грани (оси текселей): два ручья — средняя, ручей и озеро — ручья, озёра — 0
	// Глубина, к которой отнесена сила тяжести ручья: у сетки 1 м — глубина грани (равновесие — u₀), у детальной — R русла
	float  radius;
	float  baseDepth;	// глубина без камней — расход сети ∇·(h₀ u₀); 0 — грань закрыта
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
	result.radius = 0.0f;
	result.baseDepth = 0.0f;
	const int2 b = a + axes[axis];
#ifdef FLOW_DETAIL
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
	result.radius = streamA && streamB ? 0.5f * ( extraA.x + extraB.x ) : streamA ? extraA.x : extraB.x;
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
	result.radius = result.depth;
	result.baseDepth = result.depth;
#endif
	result.base = streamA && streamB ? 0.5f * ( infoA.xy + infoB.xy ) : streamA ? infoA.xy : streamB ? infoB.xy : 0.0f;
	return result;
}

// Заданная скорость грани (FLOW_DETAIL: к ячейке вне плиток) — течение сетки 1 м в точке грани
float fixedFaceVelocity( int2 cell, uint axis )
{
#ifdef FLOW_DETAIL
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
#ifdef FLOW_DETAIL
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
#ifdef FLOW_DETAIL
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
// ручьёв; у детальной — решение 1 м, у заданной грани оно же и остаётся
[numthreads( 8, 8, 1 )]
void mainFlowFaces( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	float2 velocity;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
#ifdef FLOW_DETAIL
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
		// ручья — то, что трение отнимает у скорости u₀ на глубине f.radius: у сетки 1 м это глубина грани — тогда u₀
		// равновесие (на ровном участке поле — по Маннингу), отклонения от него (инерция на изгибе и слиянии, обход
		// препятствия) гаснут за время трения; у детальной — R русла: равновесие u₀ (h / R)^(2/3), поперёк русла быстрее у оси
		const float drag = g_flowFriction / pow( max( f.depth, g_flowMinDepth ), 4.0f / 3.0f );
		const float forcing = g_flowFriction / pow( max( f.radius, g_flowMinDepth ), 4.0f / 3.0f );
		const float speed = length( axis == 0 ? float2( value, velocity.y ) : float2( velocity.x, value ) );
#ifdef FLOW_DETAIL
		// Камни — геометрия детальной сетки (дно и препятствия), сопротивление сетки 1 м не нужно
		value = ( value + forcing * length( f.base ) * f.base[axis] ) / ( 1.0f + drag * speed );
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

#ifdef FLOW_DETAIL
groupshared float4 g_nudgeSums[64];

// Крупный масштаб — к решению 1 м (после mainFlowViscosity, до давления): по свободным граням блока (группа потоков —
// блок 8 × 8) средняя по глубине разница скорости сетки 1 м и детальной, доля шага dt / nudgeTime — всем граням блока
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
	const float share = min( g_flowTimeStep / nudgeTime, 1.0f );
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
		weights += f.depth;
		sum += f.depth * pressureAt( cf.neighbor );
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
		velocity[axis] -= ( pressureAt( cell + axes[axis] ) - pressure ) / FLOW_CELL;
	}
	g_velocity[address] = velocity;
}

// Скорость ячейки — средняя двух граней по каждой оси — в текстуру для шейдеров (оси мира), у озёр и ручьёв
[numthreads( 8, 8, 1 )]
void mainFlowOutput( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	CellAddress address;
	if( !flowCell( group, thread, cell, address ) )
		return;
	const float kind = g_info[address].z;
#ifdef FLOW_DETAIL
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
#ifdef FLOW_DETAIL
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
