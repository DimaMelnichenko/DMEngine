////////////////////////////////////////////////////////////////////////////////
// Константы тонмаппинга (PostProcess, Shaders/tonemap.ps). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_TONEMAP_CONSTANTS_H
#define DM_TONEMAP_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( PostProcessBuffer, SLOT_CB_PASS )
{
	DM_FIELD( int, tonemapper );		// 0 — без тонмаппинга, 1 — ACES, 2 — AgX
	DM_FIELD( float, bloomScale );		// Bloom Intensity / сумма весов уровней: уровень хранит взвешенную сумму всех уровней
	DM_FIELD( float, purkinjeShift );	// сила ночного зрения 0…1
	DM_FIELD( float, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
