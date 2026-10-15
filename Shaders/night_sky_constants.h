////////////////////////////////////////////////////////////////////////////////
// Константы ночного неба — фон и звёзды (SkyAtmosphere::currentNightSky, Shaders/night_sky.sh). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_NIGHT_SKY_CONSTANTS_H
#define DM_NIGHT_SKY_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( NightSkyParameters, b4 )
{
	DM_FIELD( float3, equatorialX );			// экваториальный базис в мире: к точке весеннего равноденствия,
	DM_FIELD( float, moonAngularRadius );		// угловой радиус диска луны (увеличенный, как у солнца), рад
	DM_FIELD( float3, equatorialY );			// к прямому восхождению 90°,
	DM_FIELD( float, starIlluminanceScale );	// освещённость от звезды, лк → единицы запекания (1 / освещённость от солнца); 0 — звёзд нет
	DM_FIELD( float3, equatorialZ );			// к северному полюсу мира
	DM_FIELD( float, starTwinkle );			// сила мерцания звёзд, 1 — по модели, 0 — не мерцают
	DM_FIELD( float3, moonDiskLuminance );		// яркость диска полной луны (Ломмель — Зелигер: ровный диск), с цветом луны; 0 — луны нет
	DM_FIELD( float, moonDiskBrightnessLimit );	// предел яркости полного диска после экспозиции; 0 — физическая яркость
	DM_FIELD( float2, viewportSize );			// размер цели фона, пиксели
	DM_FIELD( float, milkyWayLuminance );		// средняя яркость карты Млечного Пути (её среднее по сфере — 1); 0 — его нет или день
	DM_FIELD( float, nightSkyPadding );
	// Базис тела луны в мире (либрация и позиционный угол оси, SunPosition::computeMoon): ось вращения, к нулевому
	// меридиану, к 90° восточной селенографической долготы (Море Кризисов)
	DM_FIELD( float3, moonNorth );
	DM_FIELD( float, earthshine );			// освещённость луны Землёй относительно солнечной (пепельный свет); 0 — нет
	DM_FIELD( float3, moonPrime );
	DM_FIELD( float, earthshineBrightnessLimit );	// предел яркости пепельного света после экспозиции; 0 — физическая
	DM_FIELD( float3, moonEast );
	DM_FIELD( float, nightSkyPadding2 );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
