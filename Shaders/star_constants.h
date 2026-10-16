////////////////////////////////////////////////////////////////////////////////
// Константы лучей-звёзд вокруг ярких бликов (PostProcess, Shaders/star_bright.ps, star_streak.ps). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_STAR_CONSTANTS_H
#define DM_STAR_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( StarBuffer, SLOT_CB_PASS )
{
	// star_streak.ps: вес соседней выборки по каналам — затухание луча на шаг прохода (у красного луч длиннее: дифракция
	// растёт с длиной волны)
	DM_FIELD( float3, attenuation );
	DM_FIELD( float, threshold );			// star_bright.ps: яркость после экспозиции, с которой блик даёт лучи
	DM_FIELD( float3, outputScale );		// множитель результата по каналам: последний проход оси — 2λ (луч — e^(−d/λ) от блика)
	DM_FIELD( float, limit );				// star_bright.ps: предел избытка яркости над порогом в лучах (мягкий)
	DM_FIELD( float2, direction );		// star_streak.ps: шаг выборки вдоль оси луча, uv (тексель четверти кадра × шаг прохода)
	DM_FIELD( float2, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
