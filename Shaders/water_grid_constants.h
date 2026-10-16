////////////////////////////////////////////////////////////////////////////////
// Константы счёта по плиткам детальной воды и сетке 1 м при загрузке (Shaders/water_detail_grid.sh: поле пены, фаза
// стоячих волн). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_GRID_CONSTANTS_H
#define DM_WATER_GRID_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterGridBuffer, b6 )
{
	DM_FIELD( float, gridWorldSize );		// сторона мира, м
	DM_FIELD( float, gridCellSize );		// клетка сетки 1 м, м
	DM_FIELD( float, gridTileSize );		// сторона плитки детальной воды, м
	DM_FIELD( float, gridWetDepth );		// мельче — суша, м
	DM_FIELD( uint, gridSide );				// текселей плитки детальной воды по стороне (с полем)
	DM_FIELD( uint, gridSize );				// клеток сетки 1 м по стороне
	DM_FIELD( uint, gridGroupCount );		// групп: блоков детальной воды или тайлов сетки 1 м
	DM_FIELD( uint, gridDetail );			// 1 — детальная вода у русел есть (ресурсы детальной сетки привязаны)
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
