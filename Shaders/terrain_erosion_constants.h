////////////////////////////////////////////////////////////////////////////////
// Константы эрозии рельефа (TerrainErosion, Shaders/terrain_erosion.cs). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_TERRAIN_EROSION_CONSTANTS_H
#define DM_TERRAIN_EROSION_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( TerrainErosionBuffer, b4 )
{
	DM_FIELD( uint, size );				// клеток карты по стороне (без продолжения)
	DM_FIELD( uint, padded );				// клеток по стороне с продолжением за край
	DM_FIELD( uint, pad );
	DM_FIELD( int, radius );				// радиус кисти размыва, клетки
	DM_FIELD( float, cell );				// шаг сетки, м
	DM_FIELD( float, inertia );
	DM_FIELD( float, capacity );
	DM_FIELD( float, minSlope );
	DM_FIELD( float, erodeSpeed );
	DM_FIELD( float, depositSpeed );
	DM_FIELD( float, evaporation );
	DM_FIELD( float, gravity );
	DM_FIELD( float, brushSum );			// сумма весов кисти — веса нормируются к 1
	DM_FIELD( uint, dropletCount );		// капель в пакете
	DM_FIELD( uint, batch );				// номер пакета (mainSpawn)
	DM_FIELD( uint, seed );
	DM_FIELD( float, erosionPadding0 );
	DM_FIELD( float, rainScale );			// размер пятен дождя, м
	DM_FIELD( float, rainMin );			// доля дождя в сухих местах
	DM_FIELD( float, talusTangent );		// тангенс угла откоса
	DM_FIELD( float, thermalRate );
	DM_FIELD( float3, erosionPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
