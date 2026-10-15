////////////////////////////////////////////////////////////////////////////////
// Поверхность воды (water.vs, water.ps, класс WaterSimulation): константы вызова и выход вершинного шейдера
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_SURFACE_SH
#define WATER_SURFACE_SH

#include "slots.h"

#define WATER_TILE 32

#include "water_surface_constants.h"

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
