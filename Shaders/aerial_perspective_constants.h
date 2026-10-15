////////////////////////////////////////////////////////////////////////////////
// Константы объёма воздушной перспективы (SkyAtmosphere::updateAerialPerspective, Shaders/aerial_perspective.cs). Общие
// для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_AERIAL_PERSPECTIVE_CONSTANTS_H
#define DM_AERIAL_PERSPECTIVE_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( AerialPerspectiveBuffer, b4 )
{
	DM_FIELD( float, viewDistanceScale );	// Aerial Perspective View Distance Scale: во сколько раз путь в воздухе длиннее, 0 — выключено
	DM_FIELD( float, outputScale );		// 1 / нормировка (SkyAtmosphere::aerialPerspectiveNormalization): объём — в половинной точности
	DM_FIELD( float2, aerialPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
