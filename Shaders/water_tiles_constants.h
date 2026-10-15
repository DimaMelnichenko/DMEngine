////////////////////////////////////////////////////////////////////////////////
// Константы отбора тайлов поверхности воды (WaterSurface, Shaders/water_surface.cs). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_TILES_CONSTANTS_H
#define DM_WATER_TILES_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterTilesBuffer, b5 )
{
	DM_ARRAY( float4, planes, 6 );			// frustum главного вида: нормали внутрь
	DM_FIELD( uint, indexCount );		// индексов сетки тайла
	DM_FIELD( uint, tilesPerSide );
	DM_FIELD( float, visibleDepth );		// мельче — воды не видно (только мокрая земля), м
	DM_FIELD( float, worldSize );
	DM_FIELD( float3, cameraPosition );	// главного вида
	DM_FIELD( float, detailDistance );	// м: ближе — детальная плитка (0 — детальной воды нет)
	DM_FIELD( uint, detailIndexCount );	// индексов сетки детальной плитки
	DM_FIELD( uint3, tilesPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
