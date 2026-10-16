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
	DM_FIELD( float, starScale );		// Star Intensity (0 — лучей нет): лучи-звёзды вокруг ярких бликов (star_streak.ps)
	DM_FIELD( float3, shaftColor );		// лучи солнца: сила × оттенок × видимость солнца (0 — лучей нет, light_shaft_blur.ps)
	// Лучи солнца — свет, рассеянный в воздухе перед тем, что за пикселем: доля 1 − e^(−z / расстояние), z — глубина вида;
	// 1 / расстояние, 1/м (0 — по всей глубине одинаково)
	DM_FIELD( float, shaftInverseDistance );
	DM_FIELD( float2, depthLinearize );	// _33 и _43 проекции: глубина вида z = _43 / ( d − _33 ), d — обратная глубина буфера
	DM_FIELD( float2, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
