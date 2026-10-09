////////////////////////////////////////////////////////////////////////////////
// Течение статичной воды (режим static) — установившийся расчёт при загрузке. Уровень воды уже известен (одно поле
// ручьёв и озёр, TerrainHydrology), поэтому считается только скорость: течение в озёрах под «жёсткой крышкой» —
// осреднённые по глубине уравнения мелкой воды без изменения уровня, в слое у поверхности не толще g_flowLayer (ручей
// растекается по озеру поверху; средняя по всей глубине скорость в разы меньше видимой). Расход h · u сохраняется (∇·(h u) = 0),
// скорость переносится сама собой (инерция: струя ручья входит в озеро и затухает, а не растекается от впадения во все
// стороны), гасится трением дна по Маннингу и вязкостью (перемешивание). Ручьи — заданная скорость (по Маннингу вдоль
// оси русла): у впадения они вталкивают воду в озеро, у истока — забирают (давление там — ноль, сколько пришло, столько
// и вытекает).
// Сетка разнесённая (MAC, Harlow, Welch 1965): давление — в центре ячейки, скорость — на гранях: тексель (x, y) хранит
// скорость на восточной грани (x + 1, y + ½, по оси x) и на южной (x + ½, y + 1, по оси y; оси — тексели, y — строки,
// против Z мира). Тогда расход через грань один у обеих ячеек, и поправка давлением убирает расхождение точно. Шаг
// (Stam 1999, «Stable Fluids», с весом глубины):
//   mainFlowAdvect     — перенос скорости назад по течению (полулагранжев), вязкость и трение дна;
//   mainFlowDivergence — расхождение расхода ∇·(h u);
//   mainFlowRed/Black  — давление: ∇·(h ∇p) = ∇·(h u), красно-чёрный Гаусс-Зейдель с верхней релаксацией на месте
//                        (давление прошлого шага — начальное приближение);
//   mainFlowProject    — u − ∇p.
// Ячейки: сухие (берег), озеро (считается), ручей (задан). Грани: закрыта (берег), задана (ручей; ручей, впадающий в
// озеро), свободна (озеро; озеро → исток). Класс WaterSimulation (solveFlow), docs/water.md, «Течение»
////////////////////////////////////////////////////////////////////////////////

#include "water_simulation.sh"

DM_SRV( Texture2D<float4>, g_staticWater, 3 );	// уровень ручья (x), течение по X и Z мира (y, z), уровень озера (w)
DM_SRV( StructuredBuffer<uint2>, g_flowTiles, 5 );	// тайлы счёта 8 × 8 (группа потоков — тайл): с озером и соседние

DM_UAV( RWTexture2D<float4>, g_info, 0 );		// x, y — заданная скорость ручья (оси текселей), z — вид ячейки, w — глубина текущего слоя
DM_UAV( RWTexture2D<float2>, g_velocity, 1 );	// скорость на восточной (x) и южной (y) грани, м/с
DM_UAV( RWTexture2D<float2>, g_advected, 2 );	// после переноса, вязкости и трения — до давления
DM_UAV( RWTexture2D<float>, g_pressure, 3 );	// давление (м²/с: u −= ∇p)
DM_UAV( RWTexture2D<float>, g_divergence, 4 );	// ∇·(h u), м/с
DM_UAV( RWTexture2D<float4>, g_output, 5 );		// для шейдеров (SLOT_WATER): скорость озёр — в y, z

// Раскладка — WaterSimulation::FlowParameters
cbuffer WaterFlowBuffer : register( b5 )
{
	uint  g_flowTileCount;		// тайлов в g_flowTiles
	uint3 g_flowTilesPadding;
	float g_flowTimeStep;		// с
	float g_flowViscosity;		// вихревая вязкость · dt / l² — доля, не больше 0,2 (устойчивость)
	float g_flowFriction;		// dt · g · n²: трение дна по Маннингу
	float g_flowMinDepth;		// мельче — берег, м
	float g_flowScreen;			// экранирование давления, м: озеро без истока решается, вода «уходит в грунт»
	float g_flowRelaxation;		// верхняя релаксация давления (1 — Гаусс-Зейдель)
	float g_flowLayer;			// толщина текущего слоя, м: глубже течение не считается
	float g_flowPadding;
};

static const float kindDry = 0.0f;
static const float kindLake = 1.0f;
static const float kindStream = 2.0f;
static const int2 axes[2] = { int2( 1, 0 ), int2( 0, 1 ) };

// Ячейка потока: тайл — по номеру группы. Вне тайлов текстуры счёта — нули (сухо)
bool flowCell( uint3 group, uint3 thread, out int2 cell )
{
	cell = int2( g_flowTiles[group.x] * 8 + thread.xy );
	return group.x < g_flowTileCount && inside( cell );
}

float4 cellInfo( int2 cell )
{
	return inside( cell ) ? g_info[cell] : 0.0f;
}

float2 faceVelocity( int2 cell )
{
	return inside( cell ) ? g_velocity[cell] : 0.0f;
}

// Грань между ячейкой a и соседом a + axes[axis]
struct Face
{
	uint  state;		// 0 — закрыта, 1 — задана, 2 — свободна
	float velocity;		// заданная скорость по оси грани
	float depth;		// глубина на грани
	float pressureA;	// множитель давления сторон: 1 — давление озера, 0 — давление 0 (исток в ручей)
	float pressureB;
};

static const uint faceClosed = 0;
static const uint faceFixed = 1;
static const uint faceFree = 2;

Face face( int2 a, uint axis )
{
	Face result;
	result.state = faceClosed;
	result.velocity = 0.0f;
	result.depth = 0.0f;
	result.pressureA = 0.0f;
	result.pressureB = 0.0f;
	const int2 b = a + axes[axis];
	const float4 infoA = cellInfo( a );
	const float4 infoB = cellInfo( b );
	if( infoA.z == kindDry || infoB.z == kindDry )
		return result;
	result.depth = 0.5f * ( infoA.w + infoB.w );
	if( infoA.z == kindLake && infoB.z == kindLake )
	{
		result.state = faceFree;
		result.pressureA = 1.0f;
		result.pressureB = 1.0f;
	}
	else if( infoA.z == kindStream && infoB.z == kindStream )
	{
		result.state = faceFixed;
		result.velocity = 0.5f * ( infoA[axis] + infoB[axis] );
	}
	else
	{
		// Озеро и ручей: ручей впадает — его скорость задана; вытекает из озера (исток) — грань свободна, за ней давление 0
		const bool streamIsB = infoB.z == kindStream;
		const float streamVelocity = streamIsB ? infoB[axis] : infoA[axis];
		const bool inflow = streamIsB ? streamVelocity < 0.0f : streamVelocity > 0.0f;
		if( inflow )
		{
			result.state = faceFixed;
			result.velocity = streamVelocity;
		}
		else
		{
			result.state = faceFree;
			result.pressureA = streamIsB ? 1.0f : 0.0f;
			result.pressureB = streamIsB ? 0.0f : 1.0f;
		}
	}
	return result;
}

// Ячейки и грани: так же, как mainStatic (water_simulation.cs) делит одно поле на озеро и ручей
[numthreads( 8, 8, 1 )]
void mainFlowInit( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	if( !flowCell( group, thread, cell ) )
		return;
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
	g_info[cell] = float4( fixedVelocity, kind, min( depth, g_flowLayer ) );
	g_pressure[cell] = 0.0f;
	g_divergence[cell] = 0.0f;
}

// Начальная скорость граней — заданная у заданных, 0 у остальных (после mainFlowInit: грань читает обе ячейки)
[numthreads( 8, 8, 1 )]
void mainFlowFaces( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	if( !flowCell( group, thread, cell ) )
		return;
	float2 velocity;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
		velocity[axis] = f.state == faceFixed ? f.velocity : 0.0f;
	}
	g_velocity[cell] = velocity;
	g_advected[cell] = velocity;
}

// Компонента axis скорости в точке p (ячейки, от угла сетки) — билинейно по её граням: грань текселя i по оси —
// в i + 1, по другой оси — в i + ½
float sampleComponent( float2 p, uint axis )
{
	const float2 grid = p - ( axis == 0 ? float2( 1.0f, 0.5f ) : float2( 0.5f, 1.0f ) );
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	return lerp( lerp( faceVelocity( c )[axis], faceVelocity( c + int2( 1, 0 ) )[axis], t.x ),
				 lerp( faceVelocity( c + int2( 0, 1 ) )[axis], faceVelocity( c + int2( 1, 1 ) )[axis], t.x ), t.y );
}

// Скорость в точке грани: своя компонента — с грани, другая — средняя четырёх соседних граней другой оси
float2 velocityAtFace( int2 cell, uint axis )
{
	const float own = g_velocity[cell][axis];
	const uint other = 1 - axis;
	const int2 step = axes[axis];
	const int2 back = -axes[other];
	const float across = 0.25f * ( faceVelocity( cell )[other] + faceVelocity( cell + step )[other] + faceVelocity( cell + back )[other] +
								   faceVelocity( cell + step + back )[other] );
	return axis == 0 ? float2( own, across ) : float2( across, own );
}

[numthreads( 8, 8, 1 )]
void mainFlowAdvect( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	if( !flowCell( group, thread, cell ) )
		return;
	float2 result = g_velocity[cell];
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
		if( f.state != faceFree )
			continue;
		const float2 position = cell + ( axis == 0 ? float2( 1.0f, 0.5f ) : float2( 0.5f, 1.0f ) );
		const float2 velocity = velocityAtFace( cell, axis );
		// Назад по течению на шаг (полулагранжев перенос: устойчив при любом шаге)
		float value = sampleComponent( position - velocity * g_flowTimeStep / g_cellSize, axis );
		// Вихревая вязкость — перемешивание: струя расширяется и теряет скорость; закрытые грани — 0 (берег держит воду)
		const float own = velocity[axis];
		value += g_flowViscosity * ( faceVelocity( cell + int2( 1, 0 ) )[axis] + faceVelocity( cell - int2( 1, 0 ) )[axis] +
									 faceVelocity( cell + int2( 0, 1 ) )[axis] + faceVelocity( cell - int2( 0, 1 ) )[axis] - 4.0f * own );
		// Трение дна по Маннингу, неявно: мелкая вода у берега тормозит сильнее
		const float speed = length( axis == 0 ? float2( value, velocity.y ) : float2( velocity.x, value ) );
		value /= 1.0f + g_flowFriction * speed / pow( max( f.depth, g_flowMinDepth ), 4.0f / 3.0f );
		result[axis] = value;
	}
	g_advected[cell] = result;
}

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

[numthreads( 8, 8, 1 )]
void mainFlowDivergence( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	if( !flowCell( group, thread, cell ) || g_info[cell].z != kindLake )
		return;
	float divergence = 0.0f;
	[unroll] for( uint i = 0; i < 4; ++i )
	{
		const CellFace cf = cellFace( cell, i );
		const Face f = face( cf.texel, cf.axis );
		if( f.state != faceClosed )
			divergence += cf.sign * f.depth * ( inside( cf.texel ) ? g_advected[cf.texel][cf.axis] : 0.0f );
	}
	g_divergence[cell] = divergence / g_cellSize;
}

// ∑ H (p_соседа − p) / l² − ε p = ∇·(h u) по свободным граням (сосед-ручей у истока — давление 0)
void relaxPressure( uint3 group, uint3 thread, uint parity )
{
	int2 cell;
	if( !flowCell( group, thread, cell ) || ( ( cell.x + cell.y ) & 1 ) != parity || g_info[cell].z != kindLake )
		return;
	float weights = g_flowScreen;
	float sum = -g_divergence[cell] * g_cellSize * g_cellSize;
	[unroll] for( uint i = 0; i < 4; ++i )
	{
		const CellFace cf = cellFace( cell, i );
		const Face f = face( cf.texel, cf.axis );
		if( f.state != faceFree )
			continue;
		weights += f.depth;
		if( cellInfo( cf.neighbor ).z == kindLake )
			sum += f.depth * g_pressure[cf.neighbor];
	}
	const float previous = g_pressure[cell];
	g_pressure[cell] = lerp( previous, sum / weights, g_flowRelaxation );
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

// u = u* − ∇p по свободным граням
[numthreads( 8, 8, 1 )]
void mainFlowProject( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	if( !flowCell( group, thread, cell ) )
		return;
	float2 velocity = g_advected[cell];
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		const Face f = face( cell, axis );
		if( f.state != faceFree )
			continue;
		const int2 b = cell + axes[axis];
		const float pressureA = f.pressureA > 0.0f ? g_pressure[cell] : 0.0f;
		const float pressureB = f.pressureB > 0.0f ? g_pressure[b] : 0.0f;
		velocity[axis] -= ( pressureB - pressureA ) / g_cellSize;
	}
	g_velocity[cell] = velocity;
}

// Скорость озёр — средняя двух граней по каждой оси — в текстуру для шейдеров (оси мира: y текселей против Z); ручьи
// остаются как записал mainStatic
[numthreads( 8, 8, 1 )]
void mainFlowOutput( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	if( !flowCell( group, thread, cell ) || g_info[cell].z != kindLake )
		return;
	const float2 velocity = 0.5f * ( g_velocity[cell] + float2( faceVelocity( cell - int2( 1, 0 ) ).x, faceVelocity( cell - int2( 0, 1 ) ).y ) );
	float4 state = g_output[cell];
	state.yz = float2( velocity.x, -velocity.y );
	g_output[cell] = state;
}
