////////////////////////////////////////////////////////////////////////////////
// Константы материала Color (ColorMaterial::setParams, Shaders/color.ps). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_COLOR_MATERIAL_CONSTANTS_H
#define DM_COLOR_MATERIAL_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ColorMaterialBuffer, SLOT_CB_MATERIAL )
{
	DM_FIELD( float4, objectColor );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
