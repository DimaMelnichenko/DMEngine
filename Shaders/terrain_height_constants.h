////////////////////////////////////////////////////////////////////////////////
// Высота рельефа для compute (TerrainHeightSource, Shaders/terrain_height.sh): расстановка, частицы. Общие для C++ и
// HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_TERRAIN_HEIGHT_CONSTANTS_H
#define DM_TERRAIN_HEIGHT_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( TerrainHeightBuffer, b5 )
{
	DM_FIELD( float, terrainWorldSize );
	DM_FIELD( float, terrainHeightMultiplier );
	DM_FIELD( float, terrainHeightOffset );
	DM_FIELD( float, terrainDetailTile );	// сторона детальной плитки, м (terrain_detail.sh); 0 — детальной земли нет
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
