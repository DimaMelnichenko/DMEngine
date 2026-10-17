////////////////////////////////////////////////////////////////////////////////
// Константы покрытия грани карты отражений места (ReflectionCaptures, Shaders/reflection_capture_coverage.cs). Общие для C++ и
// HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_REFLECTION_CAPTURE_CONSTANTS_H
#define DM_REFLECTION_CAPTURE_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ReflectionCaptureCoverageBuffer, SLOT_CB_PASS )
{
	DM_FIELD( int, face );			// грань: 0…5 — +X, −X, +Y, −Y, +Z, −Z
	DM_FIELD( uint, size );			// сторона грани, текселей
	DM_FIELD( float2, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
