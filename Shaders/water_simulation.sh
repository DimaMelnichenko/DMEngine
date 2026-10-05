////////////////////////////////////////////////////////////////////////////////
// Общее для compute-шейдеров воды (water_simulation.cs, water_surface.cs): константы симуляции (b4,
// WaterSimulation::Parameters), рельеф ячейки по итоговой карте высот (t0) и границы сетки
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_SIMULATION_SH
#define WATER_SIMULATION_SH

#include "bindless.sh"

DM_SRV( Texture2D<float>, g_heightMap, 0 );		// итоговая карта высот террейна (TerrainHeight::heightMap)

// Раскладка — WaterSimulation::Parameters
cbuffer WaterSimulationBuffer : register( b4 )
{
	uint  g_size;				// ячеек по стороне (= текселей карты высот)
	float g_cellSize;			// сторона ячейки, м
	float g_timeStep;			// с
	float g_gravity;			// м/с²
	float g_rain;				// дождь, м/с слоя воды
	float g_evaporation;		// испарение, м/с
	float g_sourceRate;			// приток ячейки при полном водосборе, м/с слоя воды
	float g_heightMultiplier;	// высота рельефа = карта · множитель + смещение (TerrainHeight)
	float g_heightOffset;
	float g_logFlowStart;		// log10 водосбора: от начала притока до полного
	float g_logFlowFull;
	int   g_sourceRadius;		// радиус размытия источников, ячейки
	float g_manning;			// шероховатость дна по Маннингу, с/м^(1/3)
	float3 g_simulationPadding;
};

float terrain( int2 cell )
{
	return g_heightMap.Load( int3( cell, 0 ) ) * g_heightMultiplier + g_heightOffset;
}

bool inside( int2 cell )
{
	return all( cell >= 0 ) && all( cell < (int)g_size );
}

#endif
