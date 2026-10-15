////////////////////////////////////////////////////////////////////////////////
// Поверхность воды (water.vs, water.ps, класс WaterSimulation): константы вызова и выход вершинного шейдера
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_SURFACE_SH
#define WATER_SURFACE_SH

#include "slots.h"

#define WATER_TILE 32

// Раскладка — WaterSurface::SurfaceParameters
cbuffer WaterSurfaceBuffer : register( SLOT_CB_MATERIAL )
{
	uint   g_size;				// ячеек симуляции по стороне
	float  g_cellSize;			// м
	float  g_worldSize;			// м
	uint   g_tilesPerSide;
	float3 g_absorption;		// поглощение в воде по каналам, 1/м
	float  g_rippleStretch;		// рябь быстрой воды вытянута вдоль течения во столько раз
	float3 g_scatterColor;		// цвет рассеяния в толще воды (доля освещённости неба)
	float  g_rippleScale;		// метров на повтор текстуры ряби
	float  g_rippleStrength;	// наклон нормали ряби на быстрой воде
	float  g_calmRipple;		// и на стоячей (ветер)
	float  g_refraction;		// сдвиг выборки цвета сцены по нормали, доля экрана на метр толщины
	float  g_roughness;			// шероховатость поверхности (размытие отражения неба, ширина блика)
	float  g_foamSpeed;			// пена — быстрее этого, м/с (полная — вдвое быстрее)
	float  g_foamShear;			// и там, где сдвиг скорости больше, 1/с
	float  g_detailDistance;	// м: ближе — поверхность у русел рисует детальная плитка (0 — её нет)
	float  g_foamSlope;			// пена слива — где поверхность детальной воды круче, м на м (полная — втрое круче)
	// Точная земля — урез по ней (water.ps): карта высот (слот 3) в метры и детальная плитка (terrain_detail.sh)
	float  g_terrainHeightMultiplier;
	float  g_terrainHeightOffset;
	float  g_terrainDetailTile;	// сторона детальной плитки, м; 0 — её нет
	float  g_surfacePadding;
};

struct WaterPixelInput
{
	float4 position : SV_POSITION;
	float3 worldPosition : WORLDPOS0;
	float3 normal : NORMAL0;		// нормаль поверхности воды по уровню соседних ячеек
	float2 uv : TEXCOORD0;			// UV карты высот и текстуры воды (water.sh)
	nointerpolation uint tile : TILE0;	// тайл экземпляра (номер в сетке тайлов 1 м)
};

// Тайл 1 м (строки — против Z мира) → плитка детальной земли (x, z — по Z мира): обе сетки — по 32 клетки от начала мира
int2 waterDetailTile( uint tile )
{
	return int2( tile % g_tilesPerSide, g_tilesPerSide - 1 - tile / g_tilesPerSide );
}

// Плитку рисует детальная сетка: есть детальная земля (индекс index — срез + 1) и плитка ближе g_detailDistance к камере
// по горизонтали; то же правило — у отбора тайлов (water_surface.cs, mainTiles)
bool waterDetailRendered( int2 detailTile, uint index, float3 cameraPosition )
{
	const float tileSize = WATER_TILE * g_cellSize;
	return g_detailDistance > 0.0f && index > 0 && distance( ( detailTile + 0.5f ) * tileSize, cameraPosition.xz ) < g_detailDistance;
}

#endif
