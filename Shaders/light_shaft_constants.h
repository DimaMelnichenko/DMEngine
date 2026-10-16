////////////////////////////////////////////////////////////////////////////////
// Константы лучей солнца — god rays (PostProcess, Shaders/light_shaft_mask.ps, light_shaft_blur.ps). Общие для C++ и
// HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_LIGHT_SHAFT_CONSTANTS_H
#define DM_LIGHT_SHAFT_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( LightShaftBuffer, SLOT_CB_PASS )
{
	DM_FIELD( float2, sunUV );			// экранная точка солнца, uv (может быть за краем кадра)
	DM_FIELD( float, aspect );			// ширина кадра / высота: расстояние до солнца — в долях высоты
	DM_FIELD( float, threshold );			// light_shaft_mask.ps: яркость неба после экспозиции, с которой оно светит лучами
	DM_FIELD( float, radius );			// light_shaft_mask.ps: на этом расстоянии от солнца (доля высоты кадра) маска сходит на нет
	DM_FIELD( float, limit );				// light_shaft_mask.ps: предел избытка яркости над порогом (мягкий): солнечный диск
	DM_FIELD( float, blurLength );		// light_shaft_blur.ps: доля пути к солнцу, которую собирает марш
	// light_shaft_blur.ps: спад веса выборки к солнцу (Decay у Mitchell) — вес e^(−это × доля пути), 0 — выборки равны
	DM_FIELD( float, decayRate );
	// light_shaft_blur.ps: круговое затухание следа вокруг солнца — до 1 − fadeWidth от радиуса полный, к радиусу 0 (доля
	// высоты кадра; 0 — без него): лучи не уходят до края кадра
	DM_FIELD( float, fadeRadius );
	DM_FIELD( float, fadeWidth );
	DM_FIELD( float2, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
