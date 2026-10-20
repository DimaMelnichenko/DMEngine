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
