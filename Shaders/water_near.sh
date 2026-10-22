////////////////////////////////////////////////////////////////////////////////
// Окно подробного течения у камеры (NearWaterFlow, docs/water.md, «Подробное течение у камеры») для шейдеров воды: итог
// окна и где оно — материалу (water.sh, water_shading.sh), сетке окна (water.vs с WATER_NEAR) и правилу, какая сетка где
// рисует поверхность (water_clip.sh)
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_NEAR_SH
#define WATER_NEAR_SH

#include "common.vs"
#include "samplers.sh"
#include "bindless.sh"

// Тексель — ячейка мира по модулю стороны окна (cb_waterNear: угол окна, м, ячейка, м, ячеек по стороне; 0 — окна нет).
// x — подъём поверхности по давлению, м, y, z — течение по X и Z мира, w — доля глубины над камнем (1 — камня нет, 0 — суша)
DM_SRV( Texture2D<float4>, g_waterNear, SLOT_WATER_NEAR );
// Пена окна (water_near_foam.cs): та же сетка, доля в материале — cb_waterNearFoam.x
DM_SRV( Texture2D<float>, g_waterNearFoam, SLOT_WATER_NEAR_FOAM );
// Смещения координат узора пены, которые несёт течение окна (water_near_foam.cs): слой 1 — xy, слой 2 — zw, м мира
DM_SRV( Texture2D<float4>, g_waterNearPattern, SLOT_WATER_NEAR_PATTERN );
// Фаза слоёв узора (cos, sin фазы слоя 0), её несёт течение вместе со смещениями
DM_SRV( Texture2D<float2>, g_waterNearPhase, SLOT_WATER_NEAR_PHASE );
// Пена падения: у подошвы ступени и в котле под ней (брызги бурления — по ней)
DM_SRV( Texture2D<float>, g_waterNearFall, SLOT_WATER_NEAR_FALL );
// Отклонение поверхности окна от детальной воды для мокрости: у мокрой ячейки — её, у сухой — ближайшей мокрой; −1000 — нет
DM_SRV( Texture2D<float>, g_waterNearWet, SLOT_WATER_NEAR_WET );
static const float nearWetNone = -1000.0f;

// Слоёв смещений узора два, фазы — через полпериода. Фазу несёт течение вместе со смещениями (water_near_foam.cs): у частицы
// воды она идёт ровно, обновление — когда она переходит через целое; у воды, входящей в окно (и у окна заново), фаза —
// время и шум с ячейкой nearPatternNoiseCell в точке входа: соседние струйки обновляются в разное время. Прежде фаза была
// привязана к миру, а смещения несло течение: в точку приходили смещения, обновлённые выше по течению при другой фазе, и
// на узком ручье слой обновлялся разом — пена пульсировала раз в полпериода (Dimok, 2026-10-21)
static const float nearPatternNoiseCell = 0.25f;	// м

// К краю окна у камеры на этой полосе, м, его течение переходит в детальное, а подъём поверхности сходит на нет
static const float nearWaterFade = 1.0f;
// Доля глубины над камнем, с которой ячейка окна — вода (меньше — выборка задела сушу); выше nearWaterOpen — камень под водой
static const float nearWaterWet = 0.05f;
static const float nearWaterOpen = 0.5f;

// Доля окна у камеры в точке мира: 1 внутри, к краю на полосе nearWaterFade — к 0; 0 — окна нет или точка вне него
float nearWaterWeight( float2 worldXZ )
{
	if( cb_waterNear.w <= 0.0f )
		return 0.0f;
	const float side = cb_waterNear.z * cb_waterNear.w;
	const float2 local = worldXZ - cb_waterNear.xy;
	const float2 edge = min( local, side - local );
	return saturate( min( edge.x, edge.y ) / nearWaterFade );
}

// Окно у камеры в точке мира — билинейно: ячейка мира i — в i + ½ ячейки, тексель — по модулю стороны (выборка с
// повтором; у края окна, где соседний тексель — ячейка с другой его стороны, вес окна — 0)
float4 sampleNearWater( float2 worldXZ )
{
	return g_waterNear.SampleLevel( g_SamplerLinearWrap, worldXZ / ( cb_waterNear.z * cb_waterNear.w ), 0.0f );
}

// Пена окна в точке мира — билинейно, как sampleNearWater; доля её в материале — nearWaterWeight × cb_waterNearFoam.x
float sampleNearFoam( float2 worldXZ )
{
	return g_waterNearFoam.SampleLevel( g_SamplerLinearWrap, worldXZ / ( cb_waterNear.z * cb_waterNear.w ), 0.0f );
}

// Смещения координат узора в точке мира — билинейно, как sampleNearWater
float4 sampleNearPattern( float2 worldXZ )
{
	return g_waterNearPattern.SampleLevel( g_SamplerLinearWrap, worldXZ / ( cb_waterNear.z * cb_waterNear.w ), 0.0f );
}

// PCG (Jarzynski, Olano 2020)
uint nearPatternHash( uint value )
{
	const uint state = value * 747796405u + 2891336453u;
	const uint word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

// Фаза слоя 0 узора в точке мира, периодов: время / период и плавный шум 0…1 (решётка nearPatternNoiseCell, smoothstep);
// у слоя k — + k / 2. Слой обновляется, когда его фаза переходит через целое, и его вес в этот миг — 0
float nearPatternPhase( float2 worldXZ, float time, float period )
{
	const float2 position = worldXZ / nearPatternNoiseCell;
	const int2 base = int2( floor( position ) );
	const float2 f = smoothstep( 0.0f, 1.0f, position - (float2)base );
	float noise = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = base + int2( k & 1, k >> 1 );
		const float value = ( nearPatternHash( (uint)corner.x + nearPatternHash( (uint)corner.y + 0x5bd1e995u ) ) >> 8u ) / 16777216.0f;
		noise += value * ( k & 1 ? f.x : 1.0f - f.x ) * ( k >> 1 ? f.y : 1.0f - f.y );
	}
	return time / period + noise;
}

// Случайный сдвиг узора слоя на цикл (номер цикла — целая часть фазы слоя), м: смещение слоя при обновлении — он, а не 0,
// и слой не повторяет прошлый
float2 nearPatternOffset( float cycle, uint layer )
{
	const uint hash = nearPatternHash( (uint)(int)cycle * 2u + layer + 0x27d4eb2fu );
	return float2( hash & 0xffffu, hash >> 16u ) / 65536.0f;
}

// Пена падения окна в точке мира (окно должно быть: cb_waterNear.w > 0)
float sampleNearFall( float2 worldXZ )
{
	return g_waterNearFall.SampleLevel( g_SamplerLinearWrap, worldXZ / ( cb_waterNear.z * cb_waterNear.w ), 0.0f );
}

// Фаза слоя 0 узора в точке мира, доля периода 0…1, — из окна (её несёт течение); fallback — если её там нет
float nearPatternPhaseAt( float2 worldXZ, float fallback )
{
	const float2 phase = g_waterNearPhase.SampleLevel( g_SamplerLinearWrap, worldXZ / ( cb_waterNear.z * cb_waterNear.w ), 0.0f );
	return dot( phase, phase ) > 1e-4f ? frac( atan2( phase.y, phase.x ) / 6.2831853f ) : fallback;
}

// Поправка уровня воды для мокрости в точке мира, м: в окне у камеры уровень видимой воды — детальная вода и отклонение
// мелкой воды от неё (у стенки над водой — ближайшей мокрой ячейки), билинейно по ячейкам, где оно есть; к краю окна —
// к нулю (nearWaterWeight). Без окна — 0
float nearWetLift( float2 worldXZ )
{
	const float weight = nearWaterWeight( worldXZ );
	if( weight <= 0.0f )
		return 0.0f;
	const int size = (int)cb_waterNear.w;
	const float2 position = worldXZ / cb_waterNear.z - 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	float lift = 0.0f;
	float total = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = base + corner;
		const float value = g_waterNearWet.Load( int3( ( cell % size + size ) % size, 0 ) );
		const float w = ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
		if( value > nearWetNone * 0.5f )
		{
			lift += value * w;
			total += w;
		}
	}
	return total > 1e-4f ? weight * lift / total : 0.0f;
}

// Ячейка окна (сетка мира с шагом ячейки окна от угла мира) — её тексель по модулю стороны
float4 loadNearWater( int2 cell )
{
	const int size = (int)cb_waterNear.w;
	return g_waterNear.Load( int3( ( cell % size + size ) % size, 0 ) );
}

// Точку рисует сетка окна (water.vs с WATER_NEAR): вершины — в центрах ячеек, поэтому её площадь — от центра первой ячейки
// окна до центра последней; детальные плитки здесь отсекаются
bool nearSurfaceCovers( float2 worldXZ )
{
	if( cb_waterNear.w <= 0.0f )
		return false;
	const float margin = 0.5f * cb_waterNear.z;
	const float2 local = worldXZ - cb_waterNear.xy;
	return all( local >= margin ) && all( local < cb_waterNear.z * cb_waterNear.w - margin );
}

#endif
