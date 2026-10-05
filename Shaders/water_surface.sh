////////////////////////////////////////////////////////////////////////////////
// Поверхность воды (water.vs, water.ps, класс WaterSimulation): константы вызова и выход вершинного шейдера
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_SURFACE_SH
#define WATER_SURFACE_SH

#include "slots.h"

#define WATER_TILE 32

// Раскладка — WaterSimulation::SurfaceParameters
cbuffer WaterSurfaceBuffer : register( SLOT_CB_MATERIAL )
{
	uint   g_size;				// ячеек симуляции по стороне
	float  g_cellSize;			// м
	float  g_worldSize;			// м
	uint   g_tilesPerSide;
	float3 g_absorption;		// поглощение в воде по каналам, 1/м
	float  g_flowPeriod;		// фаза текстуры течения, с (Vlachos 2010)
	float3 g_scatterColor;		// цвет рассеяния в толще воды (доля освещённости неба)
	float  g_rippleScale;		// метров на повтор текстуры ряби
	float  g_rippleStrength;	// наклон нормали ряби на быстрой воде
	float  g_calmRipple;		// и на стоячей (ветер)
	float  g_refraction;		// сдвиг выборки цвета сцены по нормали, доля экрана на метр толщины
	float  g_roughness;			// шероховатость поверхности (размытие отражения неба, ширина блика)
	float  g_foamSpeed;			// пена — быстрее этого, м/с (полная — вдвое быстрее)
	float  g_foamShear;			// и там, где сдвиг скорости больше, 1/с
	float2 g_surfacePadding;
};

struct WaterPixelInput
{
	float4 position : SV_POSITION;
	float3 worldPosition : WORLDPOS0;
	float3 normal : NORMAL0;		// нормаль поверхности воды по уровню соседних ячеек
	float2 uv : TEXCOORD0;			// UV карты высот и текстуры воды (water.sh)
};

#endif
