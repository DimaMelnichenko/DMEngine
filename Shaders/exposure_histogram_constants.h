////////////////////////////////////////////////////////////////////////////////
// Константы гистограммы яркости (PostProcess, Shaders/exposure_histogram.cs). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_EXPOSURE_HISTOGRAM_CONSTANTS_H
#define DM_EXPOSURE_HISTOGRAM_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( HistogramBuffer, b4 )
{
	DM_FIELD( uint2, sceneSize );
	DM_FIELD( float, minLog2Luminance );	// log₂ яркости нижней границы первой корзины
	DM_FIELD( float, log2LuminanceRange );	// log₂-ширина всех корзин
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
