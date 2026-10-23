////////////////////////////////////////////////////////////////////////////////
// Константы течения на детальной сетке (StaticWater, Shaders/water_flow.cs с FLOW_DETAIL). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_DETAIL_FLOW_CONSTANTS_H
#define DM_WATER_DETAIL_FLOW_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterDetailFlowBuffer, b6 )
{
	DM_FIELD( uint, detailCells );			// ячеек плитки по стороне (без поля)
	DM_FIELD( uint, detailTilesPerSide );		// плиток по стороне мира
	DM_FIELD( float, detailStep );				// сторона ячейки, м
	DM_FIELD( float, detailWetDepth );			// ячейка вне плиток — мокрая, если по сетке 1 м глубже, м
	// Валун уровня выше воды — пористое препятствие, как камень окна у камеры: глубина сквозь него — эта доля глубины без него
	// («Obstacle passage» подокна «Near flow»)
	DM_FIELD( float, detailPassage );
	DM_FIELD( float3, detailPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
