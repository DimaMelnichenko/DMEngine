////////////////////////////////////////////////////////////////////////////////
// Константы поля воды (WaterField::setParameters, Shaders/water_simulation.sh). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_SIMULATION_CONSTANTS_H
#define DM_WATER_SIMULATION_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterSimulationBuffer, b4 )
{
	DM_FIELD( uint, size );				// ячеек по стороне (= текселей карты высот)
	DM_FIELD( float, cellSize );			// сторона ячейки, м
	DM_FIELD( float, timeStep );			// с
	DM_FIELD( float, gravity );			// м/с²
	DM_FIELD( float, rain );				// дождь, м/с слоя воды
	DM_FIELD( float, evaporation );		// испарение, м/с
	DM_FIELD( float, sourceRate );			// приток ячейки при полном водосборе, м/с слоя воды
	DM_FIELD( float, heightMultiplier );	// высота рельефа = карта · множитель + смещение (TerrainHeight)
	DM_FIELD( float, heightOffset );
	DM_FIELD( float, logFlowStart );		// log10 водосбора: от начала притока до полного
	DM_FIELD( float, logFlowFull );
	DM_FIELD( int, sourceRadius );		// радиус размытия источников, ячейки
	DM_FIELD( float, manning );			// шероховатость дна по Маннингу, с/м^(1/3)
	DM_FIELD( uint, helperCount );		// источников-помощников (WaterSources) в g_helpers water_simulation.cs
	DM_FIELD( float2, simulationPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
