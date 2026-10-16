////////////////////////////////////////////////////////////////////////////////
// Константы поля пены (WaterFoam, Shaders/water_foam.cs). Общие для C++ и HLSL — shader_constants.h
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
	DM_FIELD( float, foamDecay );			// exp(−шаг / время жизни пены)
	DM_FIELD( float, foamGrowth );			// доля шага, на которую пена у источника растёт к нему
	DM_FIELD( float, foamWetDepth );		// мельче — суша, пены нет, м
	DM_FIELD( float, foamCellSize );		// клетка сетки 1 м, м
	DM_FIELD( float, foamWorldSize );		// сторона мира, м
	DM_FIELD( float, foamTileSize );		// сторона плитки детальной воды, м
	DM_FIELD( uint, foamSide );				// текселей плитки по стороне (с полем)
	DM_FIELD( uint, foamGridSize );			// клеток сетки 1 м по стороне
	DM_FIELD( uint, foamTileCount );		// групп: блоков детальной воды или тайлов сетки 1 м
	DM_FIELD( uint, foamDetail );			// 1 — детальная сетка у русел есть: у сетки 1 м источник в её плитках — её пена
	DM_FIELD( uint2, foamPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
