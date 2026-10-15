////////////////////////////////////////////////////////////////////////////////
// Константы проекции окружения на гармоники (SkyLight::captureIrradiance, Shaders/sky_irradiance.cs). Общие для C++ и
// HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SKY_IRRADIANCE_CONSTANTS_H
#define DM_SKY_IRRADIANCE_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( IrradianceParameters, b4 )
{
	DM_FIELD( float, outputScale );	// 1 / нормировка результата SkyLight
	DM_FIELD( float3, irradiancePadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
