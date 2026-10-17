////////////////////////////////////////////////////////////////////////////////
// Константы префильтра кубической карты по шероховатости (CubePrefilter, Shaders/cube_prefilter.ps): освещение неба (SkyLight) и
// карты отражений места (ReflectionCaptures). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_CUBE_PREFILTER_CONSTANTS_H
#define DM_CUBE_PREFILTER_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( PrefilterParameters, SLOT_CB_PASS )
{
	DM_FIELD( int, face );			// грань: 0…5 — +X, −X, +Y, −Y, +Z, −Z
	DM_FIELD( float, roughness );		// шероховатость текущего мипа
	DM_FIELD( float, sourceSize );		// размер грани исходного cubemap в текселях
	DM_FIELD( float, outputScale );	// множитель результата: у SkyLight — 1 / нормировка (результат в половинной точности)
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
