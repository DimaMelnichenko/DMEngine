////////////////////////////////////////////////////////////////////////////////
// Каскады теней солнца для приёма тени (ShadowCascades::bindForReceivers, Shaders/shadows.sh). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SHADOW_CONSTANTS_H
#define DM_SHADOW_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

// Каскадов теней солнца (ShadowCascades::cascadeCount)
#define SHADOW_CASCADE_COUNT 4

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ShadowConstants, SLOT_CB_SHADOW )
{
	DM_ARRAY( matrix, cascadeViewProjection, SHADOW_CASCADE_COUNT );
	DM_FIELD( matrix, farViewProjection );	// дальний каскад: срез SHADOW_CASCADE_COUNT карты
	DM_FIELD( float4, cascadeSplits );		// дальняя граница каскада по глубине взгляда, м
	DM_FIELD( float4, cascadeTexelSize );	// размер текселя каскада в мире, м
	DM_FIELD( float4, farShadow );			// x — тексель дальнего каскада, м; y — 1: он есть
	// Тень рельефа дальше каскадов: x — 1 / сторона карты, м⁻¹ (0 — карты нет); y — тангенс угла полутени; z — наименьшая
	// полутень, м; w — 1: есть тень гор на карте (SLOT_TERRAIN_MOUNTAIN_SHADOW)
	DM_FIELD( float4, terrainShadow );
	// Тень на кольце гор (SLOT_MOUNTAIN_SUN_SHADOW): x — 1 / его сторона, м⁻¹ (0 — нет); y, z — его угол x, z, м
	DM_FIELD( float4, mountainShadow );
	DM_FIELD( float, cascadeTransition );	// доля каскада в его конце, где он смешивается со следующим
	DM_FIELD( float, shadowFadeStart );	// отсюда тень плавно уходит…
	DM_FIELD( float, shadowDistance );	// …и здесь заканчивается (Dynamic Shadow Distance), м
	DM_FIELD( float, shadowNormalBias );	// сдвиг точки по нормали, в текселях каскада
	DM_FIELD( int, shadowSunIndex );	// индекс солнца в g_lights, −1 — теней нет
	DM_FIELD( int, showCascades );		// 1 — подкрасить каскады
	DM_FIELD( float, shadowMapSize );		// размер среза, текселей
	DM_FIELD( float, shadowDepthBias );	// сдвиг точки к солнцу, в текселях каскада
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
