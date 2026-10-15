////////////////////////////////////////////////////////////////////////////////
// Константы поля следов в траве (FoliageInteraction, Shaders/foliage_interaction.cs). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_FOLIAGE_INTERACTION_CONSTANTS_H
#define DM_FOLIAGE_INTERACTION_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

// Следов за кадр (FoliageInteraction::maxInteractors)
#define FOLIAGE_MAX_INTERACTORS 8

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( FoliageInteractionBuffer, b4 )
{
	DM_FIELD( int2, origin );			// первая клетка окна кадра
	DM_FIELD( int2, previousOrigin );	// и прошлого
	DM_FIELD( float, cellSize );			// м
	DM_FIELD( float, keep );				// доля следа, что остаётся за кадр
	DM_FIELD( uint, resolution );
	DM_FIELD( uint, interactorCount );
	DM_ARRAY( float4, segments, FOLIAGE_MAX_INTERACTORS );	// xy — начало, zw — конец капсулы, XZ мира
	DM_ARRAY( float4, shapes, FOLIAGE_MAX_INTERACTORS );	// x — радиус, м; y — сила 0…1; zw — направление движения × «Forward push»
	DM_ARRAY( float4, edges, FOLIAGE_MAX_INTERACTORS );		// x — ширина мягкого края, м («Edge width»)
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
