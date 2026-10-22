////////////////////////////////////////////////////////////////////////////////
// Пена окна подробного течения у камеры (NearWaterFlow, docs/water.md, «Подробное течение у камеры»): своё поле пены на
// сетке окна (6,25 см) каждый кадр — по тем же правилам, что поле пены статичной воды (water_foam.sh), но по течению окна:
// пена рождается там, где вода бьёт в камень, выходящий из воды, и где её тонкий слой бежит по камню под самой
// поверхностью, на сдвиге скорости (струя между камнями, край затишья за камнем), на быстрой воде, на нижней части падения
// со ступени и в котле под ним (верх падения — чистая вода); течение окна уносит её хвостами за камни и вокруг них, она
// тает и густеет там, где течение сходится. Вошедшие в окно ячейки и то, что
// течение приносит из-за края окна, — пена поля 0,25 м (WaterFoam). Тексель — ячейка мира по модулю стороны окна, как у
// течения (mainFlowOutput в water_flow.cs); окно — константы b7 этого кадра (cb_waterNear — с прошлого расчёта окна).
// Там же — смещения координат узора пены (advected textures, Neyret 2003; узор — nearFoamPattern в water_shading.sh): два
// слоя смещений несёт течение окна (точка узора, которая сейчас здесь, пришла оттуда, где была при обновлении слоя), слой
// обновляется (смещение — случайный сдвиг цикла), когда его фаза — её тоже несёт течение — переходит через целое
// (nearPatternPhase, water_near.sh)
////////////////////////////////////////////////////////////////////////////////

#include "water.sh"
#include "water_near_flow_constants.h"
#include "water_foam.sh"

DM_SRV( Texture2D<float4>, g_nearState, 0 );		// итог окна этого кадра: подъём, течение по X и Z мира, доля глубины над камнем
DM_SRV( Texture2D<float>, g_nearFoamPrevious, 1 );	// пена окна прошлого кадра
DM_SRV( Texture2DArray<float>, g_detailFoam, 2 );	// поле пены детальной сетки (WaterFoam): тексели и срезы детальной воды
DM_SRV( Texture2D<float4>, g_nearPatternPrevious, 3 );	// смещения координат узора прошлого кадра: слой 1 — xy, слой 2 — zw, м
DM_SRV( Texture2D<float2>, g_nearPhasePrevious, 4 );	// фаза слоёв узора прошлого кадра: cos, sin фазы слоя 0

DM_UAV( RWTexture2D<float>, g_nearFoam, 0 );
DM_UAV( RWTexture2D<float4>, g_nearPattern, 1 );
DM_UAV( RWTexture2D<float2>, g_nearPhase, 2 );
DM_UAV( RWTexture2D<float>, g_nearFall, 3 );		// бурление падения ячейки (SLOT_WATER_NEAR_FALL): по нему — брызги бурления
DM_UAV( RWTexture2D<float>, g_nearWet, 4 );		// поправка уровня для мокрости (SLOT_WATER_NEAR_WET)


// Скачок скорости для сдвига — через две ячейки в каждую сторону (25 см), делится на столько, м: как у сетки 1 м и
// детальной (там скачок через 0,5 м — на 2 м), — тот же порог foam_shear ловит струю у камня, а не профиль поперёк ручья
static const int shearReach = 2;
static const float shearSpan = 2.0f;
// Камень выше воды бьёт воду ячейкам не дальше стольких ячеек от себя
static const int stoneReach = 2;
// Падение со ступени — шагами по две ячейки (12,5 см), до метра в каждую сторону
static const float fallStepCells = 2.0f;
static const uint fallSteps = 8;

// Ячейка мира → тексель окна по модулю его стороны
uint2 nearAddress( int2 cell )
{
	const int size = (int)g_nearSize;
	return uint2( ( cell % size + size ) % size );
}

bool insideWindow( int2 cell, int2 origin )
{
	const int2 local = cell - origin;
	return all( local >= 0 ) && all( local < (int)g_nearSize );
}

// Итог окна в ячейке; вне окна — fallback
float4 nearState( int2 cell, float4 fallback )
{
	return insideWindow( cell, g_nearOrigin ) ? g_nearState[nearAddress( cell )] : fallback;
}

// Тексель детальной воды под точкой мира (water.sh: x, y — в текселях плитки с полем, z — срез); false — плитки нет
bool detailTexelAtWorld( float2 world, out float3 texel )
{
	return waterDetailTexel( float2( world.x, cb_waterDetail.x - world.y ) / cb_waterDetail.x, texel );
}

// Поле пены детальной сетки в точке мира: вне плиток — 0
float detailFoamAt( float2 world )
{
	float3 texel;
	if( !detailTexelAtWorld( world, texel ) )
		return 0.0f;
	uint width, height, slices;
	g_detailFoam.GetDimensions( width, height, slices );
	return g_detailFoam.SampleLevel( g_SamplerLinearClamp, float3( texel.xy / float2( width, height ), texel.z ), 0.0f );
}

// Уровень детальной воды в точке мира (тексель под точкой); сухо или плитки нет — fallback
float detailLevelAt( float2 world, float fallback )
{
	float3 texel;
	if( !detailTexelAtWorld( world, texel ) )
		return fallback;
	const float level = g_waterDetail.Load( int4( int2( texel.xy ), texel.z, 0 ) ).x;
	return level > -1e29f ? level : fallback;
}

// Уровень поверхности окна в точке мира: детальная вода и отклонение окна от неё (x итога; вне окна — 0); сухо или плитки
// нет — fallback (water_foam.sh, traceFall)
float fallLevelAt( float2 world, float fallback )
{
	const float level = detailLevelAt( world, -1e30f );
	if( level < -1e29f )
		return fallback;
	const float4 state = nearState( int2( floor( world / g_nearStep ) ), float4( 0.0f, 0.0f, 0.0f, 1.0f ) );
	return state.w > 0.0f ? level + state.x : fallback;
}

// Глубина детальной воды в точке мира (без камней окна)
float detailDepthAt( float2 world )
{
	float3 texel;
	return detailTexelAtWorld( world, texel ) ? max( g_waterDetail.Load( int4( int2( texel.xy ), texel.z, 0 ) ).w, 0.0f ) : 0.0f;
}

// Пена прошлого кадра в точке мира — билинейно по ячейкам окна прошлого кадра; ячейки вне него — поле пены 0,25 м
float carriedFoam( float2 world )
{
	const float2 position = world / g_nearStep - 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	float foam = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = base + corner;
		const float value = insideWindow( cell, g_nearPreviousOrigin ) ? g_nearFoamPrevious[nearAddress( cell )] :
																		 detailFoamAt( ( cell + 0.5f ) * g_nearStep );
		foam += value * ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
	}
	return foam;
}

// Смещения узора прошлого кадра в точке мира — билинейно по ячейкам окна прошлого кадра; вне него — 0 (узор только что
// обновлён: у края окна вес узора по течению — 0)
float4 carriedPattern( float2 world )
{
	const float2 position = world / g_nearStep - 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	float4 offsets = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = base + corner;
		if( insideWindow( cell, g_nearPreviousOrigin ) )
			offsets += g_nearPatternPrevious[nearAddress( cell )] * ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
	}
	return offsets;
}

// Фаза слоя 0 узора прошлого кадра в точке мира, доля периода 0…1: у ячеек окна прошлого кадра — их (cos, sin) билинейно, у
// воды извне окна — время и шум в точке (nearPatternPhase)
float carriedPhase( float2 world )
{
	const float2 position = world / g_nearStep - 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	const float outside = frac( nearPatternPhase( world, g_nearPatternTime - g_nearPatternStep, g_nearPatternPeriod ) );
	const float2 outsideVector = float2( cos( 6.2831853f * outside ), sin( 6.2831853f * outside ) );
	float2 phase = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = base + corner;
		const float2 value = insideWindow( cell, g_nearPreviousOrigin ) ? g_nearPhasePrevious[nearAddress( cell )] : outsideVector;
		phase += value * ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
	}
	return dot( phase, phase ) > 1e-6f ? frac( atan2( phase.y, phase.x ) / 6.2831853f ) : outside;
}

// Сухая ячейка (стенка русла над водой) берёт для мокрости отклонение ближайшей мокрой не дальше стольких ячеек (~19 см):
// мокрая полоса в несколько сантиметров над водой у крутой стенки — в паре ячеек от уреза, у пологого берега дальше — уже
// детальная вода
static const int nearWetReach = 3;

// Отклонение поверхности окна от детальной воды для мокрости в ячейке: мокрая — своё, сухая — ближайшей мокрой; nearWetNone
float wetLift( int2 cell, float4 here )
{
	if( here.w > 0.0f )
		return here.x;
	float nearest = 1e9f;
	float lift = nearWetNone;
	[loop] for( int y = -nearWetReach; y <= nearWetReach; ++y )
		[loop] for( int x = -nearWetReach; x <= nearWetReach; ++x )
		{
			const float4 state = nearState( cell + int2( x, y ), float4( 0.0f, 0.0f, 0.0f, 0.0f ) );
			const float distance = x * x + y * y;
			if( state.w > 0.0f && distance < nearest )
			{
				nearest = distance;
				lift = state.x;
			}
		}
	return lift;
}

// Пена ячейки за кадр (here — итог окна в ячейке, вода есть); churn — бурление падения у подошвы и в котле (fallChurn)
float stepFoam( int2 cell, float2 world, float4 here, out float churn );

[numthreads( 8, 8, 1 )]
void mainNearFoam( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_nearSize ) )
		return;
	const int2 cell = g_nearOrigin + int2( id.xy );
	const uint2 address = nearAddress( cell );
	const float2 world = ( cell + 0.5f ) * g_nearStep;
	const float4 here = g_nearState[address];
	g_nearWet[address] = wetLift( cell, here );
	// Суша (и камень на берегу) — без пены; вошла в окно в этом кадре (или окно заново) — пена поля 0,25 м, узор — обновлён
	const bool fresh = g_nearReset != 0 || !insideWindow( cell, g_nearPreviousOrigin );
	if( here.w <= 0.0f || fresh )
	{
		const float phase = 6.2831853f * frac( nearPatternPhase( world, g_nearPatternTime, g_nearPatternPeriod ) );
		g_nearFoam[address] = here.w > 0.0f ? detailFoamAt( world ) : 0.0f;
		g_nearPattern[address] = 0.0f;
		g_nearPhase[address] = float2( cos( phase ), sin( phase ) );
		g_nearFall[address] = 0.0f;
		return;
	}
	float churn;
	g_nearFoam[address] = stepFoam( cell, world, here, churn );
	g_nearFall[address] = churn;

	// Смещения узора и фаза: назад по течению, смещение — на сдвиг за кадр, фаза — на долю периода; слой, чья фаза перешла
	// через целое, — заново: смещение — случайный сдвиг цикла (номер цикла — фаза частицы без дробной части: время / период
	// и её шум в точке входа, у соседних частиц на той же границе цикла — тот же)
	const float2 from = world - g_nearPatternStep * here.yz;
	float4 offsets = carriedPattern( from ) - g_nearPatternStep * here.yzyz;
	const float before = carriedPhase( from );
	const float phase = before + g_nearPatternStep / g_nearPatternPeriod;
	const float time = g_nearPatternTime / g_nearPatternPeriod;
	const float cycle = floor( time + frac( phase - time ) + 0.5f );
	if( floor( phase ) != floor( before ) )
		offsets.xy = nearPatternOffset( cycle, 0 );
	if( floor( phase + 0.5f ) != floor( before + 0.5f ) )
		offsets.zw = nearPatternOffset( floor( time + frac( phase + 0.5f - time ) + 0.5f ), 1 );
	g_nearPattern[address] = offsets;
	g_nearPhase[address] = float2( cos( 6.2831853f * phase ), sin( 6.2831853f * phase ) );
}

float stepFoam( int2 cell, float2 world, float4 here, out float churn )
{
	const float2 velocity = here.yz;
	const float speed = length( velocity );

	// Сдвиг скорости и сжатие течения — по соседям окна (за краем окна и у суши — как сама ячейка)
	const float4 east = nearState( cell + int2( shearReach, 0 ), here );
	const float4 west = nearState( cell - int2( shearReach, 0 ), here );
	const float4 north = nearState( cell + int2( 0, shearReach ), here );
	const float4 south = nearState( cell - int2( 0, shearReach ), here );
	const float2 dvdx = ( ( east.w > 0.0f ? east.yz : velocity ) - ( west.w > 0.0f ? west.yz : velocity ) ) / ( 2.0f * shearSpan );
	const float2 dvdz = ( ( north.w > 0.0f ? north.yz : velocity ) - ( south.w > 0.0f ? south.yz : velocity ) ) / ( 2.0f * shearSpan );
	float4 n[4];
	n[0] = nearState( cell + int2( 1, 0 ), here );
	n[1] = nearState( cell - int2( 1, 0 ), here );
	n[2] = nearState( cell + int2( 0, 1 ), here );
	n[3] = nearState( cell - int2( 0, 1 ), here );
	const float divergence = ( n[0].y - n[1].y + n[2].z - n[3].z ) / ( 2.0f * g_nearStep );

	float source = foamSource( speed, strainRate( dvdx, dvdz ) );

	// Камень у поверхности. В самом камне выше воды пену закрывает камень, а течение сквозь него — просачивание: не источник
	const float fast = saturate( speed / stoneFoamSpeed );
	if( here.w >= nearWaterOpen )
	{
		// Тонкий слой над камнем под самой поверхностью (доля глубины над камнем × глубина без камней)
		if( here.w < 0.95f )
			source += saturate( 1.0f - here.w * detailDepthAt( world ) / stoneFoamDepth ) * fast;
		// Вода бьёт в камень выше воды: ячейки перед камнем по течению (с боков — вдвое слабее, за ним — нет)
		const float2 along = speed > 1e-3f ? velocity / speed : float2( 0.0f, 0.0f );
		float impact = 0.0f;
		[unroll] for( int r = 1; r <= stoneReach; ++r )
			[unroll] for( uint k = 0; k < 4; ++k )
			{
				const int2 offset = k == 0 ? int2( r, 0 ) : k == 1 ? int2( -r, 0 ) : k == 2 ? int2( 0, r ) : int2( 0, -r );
				const float4 neighbor = nearState( cell + offset, here );
				if( neighbor.w > 0.0f && neighbor.w < nearWaterOpen )
					impact = max( impact, saturate( 0.5f + 0.5f * dot( along, normalize( float2( offset ) ) ) ) );
			}
		source += impact * fast;
	}

	// Падение со ступени — по поверхности окна (в мелкой воде — её ступени, плёсы и прыжки): ближе foamFallStart к кромке
	// источников нет (и у быстрой воды, и у камней) — верх чистый; ниже, у подошвы и в котле — пена падения, сразу белая
	// вода (воздух в поток несёт сама струя), а не пена, которая растёт к источнику за время
	float fall = 0.0f;
	churn = 0.0f;
	const float level = fallLevelAt( world, -1e30f );
	if( speed > 1e-3f && level > -1e29f )
	{
		const Fall trace = traceFall( world, velocity / speed, level, fallStepCells * g_nearStep, fallSteps );
		source *= fallFoamShare( trace );
		fall = fallFoam( trace );
		churn = fallChurn( trace );
	}

	// Назад по течению (смещение за кадр — доли ячейки: скорость в середине шага та же)
	const float carried = carriedFoam( world - g_foamTimeStep * velocity );
	return max( foamStep( carried, divergence, source, speed ), fall );
}
