////////////////////////////////////////////////////////////////////////////////
// Общее для compute-шейдеров воды (water_simulation.cs, water_surface.cs): константы симуляции (b4,
// water_simulation_constants.h), рельеф ячейки по итоговой карте высот (t0) и границы сетки
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_SIMULATION_SH
#define WATER_SIMULATION_SH

#include "bindless.sh"

DM_SRV( Texture2D<float>, g_heightMap, 0 );		// итоговая карта высот террейна (TerrainHeight::heightMap)

#include "water_simulation_constants.h"

float terrain( int2 cell )
{
	return g_heightMap.Load( int3( cell, 0 ) ) * g_heightMultiplier + g_heightOffset;
}

bool inside( int2 cell )
{
	return all( cell >= 0 ) && all( cell < (int)g_size );
}

#endif
