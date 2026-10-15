////////////////////////////////////////////////////////////////////////////////
// Константы наложения облаков на фон (VolumetricCloud, Shaders/cloud_composite.ps). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_CLOUD_COMPOSITE_CONSTANTS_H
#define DM_CLOUD_COMPOSITE_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( CloudCompositeBuffer, b4 )
{
	DM_FIELD( float, cloudScale );	// нормировка неба (SkyAtmosphere::skyNormalization): свет в текстуре × она × cb_skyScale — кд/м²
	DM_FIELD( float3, compositePadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
