////////////////////////////////////////////////////////////////////////////////
// Константы расчёта течения (StaticWater, Shaders/water_flow.cs). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_WATER_FLOW_CONSTANTS_H
#define DM_WATER_FLOW_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( WaterFlowBuffer, b5 )
{
	DM_FIELD( uint, flowTileCount );		// групп в g_flowTiles
	DM_FIELD( uint3, flowTilesPadding );
	DM_FIELD( float, flowTimeStep );		// с
	DM_FIELD( float, flowViscosity );		// вихревая вязкость · dt / l² — доля, не больше 0,2 (устойчивость)
	DM_FIELD( float, flowFriction );		// dt · g · n²: трение дна по Маннингу в озере
	DM_FIELD( float, flowStreamFriction );	// то же у ручья — n русел, по которому считана u₀
	DM_FIELD( float, flowMinDepth );		// мельче — берег, м
	DM_FIELD( float, flowScreen );			// экранирование давления, м: озеро без истока решается, вода «уходит в грунт»
	DM_FIELD( float, flowRelaxation );		// верхняя релаксация давления (1 — Гаусс-Зейдель)
	DM_FIELD( float, flowLayer );			// толщина текущего слоя, м: глубже течение не считается
	DM_FIELD( float, flowStoneDrag );		// dt · Cd / 2: сопротивление камней русла (сетка 1 м)
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
