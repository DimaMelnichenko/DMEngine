////////////////////////////////////////////////////////////////////////////////
// Константы ледниковой эрозии (GlacialErosion, Shaders/glacial_erosion.cs). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_GLACIAL_EROSION_CONSTANTS_H
#define DM_GLACIAL_EROSION_CONSTANTS_H

#include "shader_constants.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( GlacialErosionBuffer, b4 )
{
	DM_FIELD( uint, size );				// клеток сетки льда по стороне
	DM_FIELD( float, cell );				// шаг сетки, м
	DM_FIELD( float, timeStep );			// шаг, лет
	DM_FIELD( float, diffusionCap );		// предел «диффузии» льда D, м²/год: шаг устойчив при D · шаг / клетка² ≤ 0,2
	DM_FIELD( float, deformation );		// течение (закон Глена, n = 3): D_d = deformation · H⁵ · |∇s|², м⁻³·год⁻¹
	DM_FIELD( float, sliding );			// скольжение: u_s = sliding · H² · |∇s|², м⁻¹·год⁻¹
	DM_FIELD( float, ela );				// линия равновесия, м: выше копится, ниже тает
	DM_FIELD( float, balanceGradient );	// баланс массы на метр высоты над ELA, 1/год
	DM_FIELD( float, maxAccumulation );	// наибольшее накопление, м/год
	DM_FIELD( float, erosion );			// размыв ложа — эта доля скорости скольжения, м на м пути
	DM_FIELD( float, maxErosion );		// ложе ниже исходного не больше, м
	DM_FIELD( uint, erode );				// 1 — шаг с эрозией (иначе лёд только набирается)
	DM_FIELD( float2, origin );			// угол сетки с наименьшими x, z мира, м (строка 0 — север)
	DM_FIELD( float, mapSize );			// карта уровня — квадрат [0, mapSize]² мира: у неё эрозия гаснет…
	DM_FIELD( float, padding );
	DM_FIELD( float2, mapFade );			// …от x до y м от её края эрозия набирает полную силу
	DM_FIELD( float2, padding2 );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
