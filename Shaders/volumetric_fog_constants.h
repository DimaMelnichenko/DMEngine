////////////////////////////////////////////////////////////////////////////////
// Константы объёма тумана (VolumetricFog, Shaders/volumetric_fog.cs). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_VOLUMETRIC_FOG_CONSTANTS_H
#define DM_VOLUMETRIC_FOG_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( VolumetricFogBuffer, b4 )
{
	DM_FIELD( matrix, previousViewProjection );	// главный вид прошлого кадра
	DM_FIELD( float3, jitter );			// сдвиг точки в ячейке этого кадра, 0…1
	DM_FIELD( float, historyWeight );		// доля прошлого кадра; 0 — смена плана, прошлого нет
	DM_FIELD( uint3, gridSize );
	DM_FIELD( float, historyScale );		// нормировка прошлого кадра / нынешняя
	DM_FIELD( float, inverseScale );		// 1 / cb_fogScale: объём хранит свет, делённый на нормировку
	DM_FIELD( float3, fogPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
