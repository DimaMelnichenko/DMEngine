////////////////////////////////////////////////////////////////////////////////
// Константы поля пены (WaterFoam, Shaders/water_foam.cs; сетки — water_grid_constants.h). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_FOAM_CONSTANTS_H
#define DM_WATER_FOAM_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterFoamBuffer, b5 )
{
	DM_FIELD( float, foamSpeed );			// пена — быстрее этого, м/с (полная — вдвое быстрее)
	DM_FIELD( float, foamShear );			// и где сдвиг скорости больше, 1/с
	DM_FIELD( float, foamSlope );			// и где поверхность круче (слив), м на м
	DM_FIELD( float, foamTimeStep );		// шаг установления, с
	DM_FIELD( float, foamLifetime );		// время жизни пены на быстрой воде, с
	DM_FIELD( float, foamStillLifetime );	// и в стоячей, с
	DM_FIELD( float, foamGrowth );			// доля шага, на которую пена у источника растёт к нему
	DM_FIELD( float, foamPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
