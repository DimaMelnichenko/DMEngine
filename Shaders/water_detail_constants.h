////////////////////////////////////////////////////////////////////////////////
// Константы детальной воды у русел (StaticWater, Shaders/water_detail.cs). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_DETAIL_CONSTANTS_H
#define DM_WATER_DETAIL_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterDetailBuffer, b5 )
{
	DM_FIELD( float, heightMultiplier );
	DM_FIELD( float, heightOffset );
	DM_FIELD( float, worldSize );
	DM_FIELD( float, tileSize );			// сторона плитки, м
	DM_FIELD( float, visibleDepth );		// мельче — воды не видно (мокрая земля), м
	DM_FIELD( uint, side );				// текселей плитки по стороне (с полем)
	DM_FIELD( float2, detailPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
