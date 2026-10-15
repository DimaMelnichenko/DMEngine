////////////////////////////////////////////////////////////////////////////////
// Константы адаптации экспозиции (PostProcess, Shaders/exposure_adapt.cs). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_EXPOSURE_ADAPT_CONSTANTS_H
#define DM_EXPOSURE_ADAPT_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

// Ключей кривой компенсации экспозиции (PostProcess::maxCurveKeys)
#define MAX_CURVE_KEYS 8

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( AdaptBuffer, b4 )
{
	DM_FIELD( int, meteringMode );			// 0 — Manual, 1 — Auto Exposure Histogram
	DM_FIELD( float, manualEV100 );
	DM_FIELD( float, exposureCompensation );	// EV
	DM_FIELD( float, deltaTime );				// с
	DM_FIELD( float, minEV100 );
	DM_FIELD( float, maxEV100 );
	DM_FIELD( float, lowPercent );				// доля 0…1: темнее — не в среднем
	DM_FIELD( float, highPercent );			// доля 0…1: ярче — не в среднем
	DM_FIELD( float, speedUp );				// скорость адаптации, 1/с
	DM_FIELD( float, speedDown );
	DM_FIELD( float, minLog2Luminance );		// диапазон гистограммы — как у exposure_histogram.cs
	DM_FIELD( float, log2LuminanceRange );
	DM_FIELD( int, curveKeyCount );			// ключей кривой компенсации; 0 — без кривой
	DM_FIELD( float3, adaptPadding );
	DM_ARRAY( float4, curveKeys, MAX_CURVE_KEYS / 2 );	// по два ключа (EV100 сцены, поправка EV), по возрастанию EV100
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
