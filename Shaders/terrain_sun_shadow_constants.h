////////////////////////////////////////////////////////////////////////////////
// Константы карты края тени рельефа (TerrainSunShadow, Shaders/terrain_sun_shadow.cs). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_TERRAIN_SUN_SHADOW_CONSTANTS_H
#define DM_TERRAIN_SUN_SHADOW_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( TerrainSunShadowBuffer, b4 )
{
	DM_FIELD( float2, sunAzimuth );		// направление на солнце в плоскости XZ мира, нормированное
	DM_FIELD( float, sunSlope );			// тангенс высоты солнца
	DM_FIELD( float, texelSize );			// м на тексель результата
	DM_FIELD( float, heightMultiplier );	// высота = значение карты × множитель + сдвиг (TerrainHeight)
	DM_FIELD( float, heightOffset );
	DM_FIELD( float, worldSize );			// сторона карты, м (начало — 0, 0; v = 1 − z / сторона)
	DM_FIELD( float, maxHeight );			// наибольшая высота рельефа, м: выше луча ничего нет — дальше не идти
	DM_FIELD( float, mapTexel );			// м на тексель карты высот (мип 0)
	DM_FIELD( float, startDistance );		// с какого расстояния заслоняет (соседние тексели — нет: свет на склоне даёт N·L)
	DM_FIELD( uint, size );				// текселей результата по стороне
	DM_FIELD( float, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
