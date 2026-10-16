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
	DM_FIELD( float2, areaOrigin );		// угол области результата с наименьшими x, z, м (карта — 0, 0; горы — их угол)
	DM_FIELD( uint, size );				// текселей результата по стороне
	DM_FIELD( float, startDistance );		// с какого расстояния заслоняет (соседние тексели — нет: свет на склоне даёт N·L)
	DM_FIELD( float, heightMultiplier );	// карта уровня: высота = значение × множитель + сдвиг (TerrainHeight)
	DM_FIELD( float, heightOffset );
	DM_FIELD( float, worldSize );			// её сторона, м (начало — 0, 0; v = 1 − z / сторона); 0 — карты нет
	DM_FIELD( float, mapTexel );			// м на тексель карты высот (мип 0)
	DM_FIELD( float2, farOrigin );		// горы вокруг карты (FarTerrain): угол, м
	DM_FIELD( float, farExtent );			// их сторона, м; 0 — гор нет
	DM_FIELD( float, farTexel );			// м на тексель их карты (мип 0; высоты — в метрах)
	DM_FIELD( float, maxHeight );			// наибольшая высота рельефа, м: выше луча ничего нет — дальше не идти
	// 1 — заслоны за краем карты (горы) пишутся отдельно, во вторую цель: их тени нет в каскадах — её берут и вблизи
	DM_FIELD( uint, external );
	DM_FIELD( float2, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
