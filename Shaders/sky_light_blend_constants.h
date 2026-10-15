////////////////////////////////////////////////////////////////////////////////
// Константы перехода освещения окружением от прежнего результата к новому (SkyLight::blend): Shaders/sky_light_blend.ps
// — грани префильтра, sky_irradiance_blend.cs — гармоники. Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SKY_LIGHT_BLEND_CONSTANTS_H
#define DM_SKY_LIGHT_BLEND_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( SkyLightBlendBuffer, SLOT_CB_PASS )
{
	DM_FIELD( int, face );
	DM_FIELD( float, mip );
	DM_FIELD( float, previousWeight );	// доля прежнего результата — с поправкой на его нормировку
	DM_FIELD( float, currentWeight );	// доля нового
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
