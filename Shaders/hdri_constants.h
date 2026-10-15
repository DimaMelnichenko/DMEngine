////////////////////////////////////////////////////////////////////////////////
// Константы панорамы (HDRIBackdrop, Shaders/hdri.sh). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_HDRI_CONSTANTS_H
#define DM_HDRI_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( HDRIParameters, SLOT_CB_PASS )
{
	DM_FIELD( int, face );			// грань cubemap освещения: 0…5 — +X, −X, +Y, −Y, +Z, −Z
	DM_FIELD( float, rotation );		// поворот панорамы вокруг вертикали, радианы
	DM_FIELD( float, maxLuminance );	// срез яркости для освещения окружением, в единицах панорамы; 0 — без среза
	DM_FIELD( float, lod );			// мип панорамы для граней cubemap
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
