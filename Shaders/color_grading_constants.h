////////////////////////////////////////////////////////////////////////////////
// Константы таблицы цветокоррекции (ColorGradingLut, Shaders/color_grading_lut.cs). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_COLOR_GRADING_CONSTANTS_H
#define DM_COLOR_GRADING_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ColorGradingBuffer, b4 )
{
	DM_ARRAY( float4, whiteBalance, 3 );	// строки матрицы баланса белого в линейном sRGB (w — 0)
	// Тени, полутона, света (по номеру): общее значение × своё, каналы × общий множитель (w окна) — rgb
	DM_ARRAY( float4, saturation, 3 );
	DM_ARRAY( float4, contrast, 3 );
	DM_ARRAY( float4, gamma, 3 );
	DM_ARRAY( float4, gain, 3 );
	DM_ARRAY( float4, offset, 3 );			// общее + своё, каналы + общий сдвиг
	DM_FIELD( float, shadowsMax );			// доля теней — 1 − smoothstep( 0, shadowsMax, яркость AP1 )
	DM_FIELD( float, highlightsMin );		// доля светов — smoothstep( highlightsMin, highlightsMax, яркость AP1 )
	DM_FIELD( float, highlightsMax );
	DM_FIELD( int, tonemapper );			// 0 — без тонмаппинга, 1 — ACES, 2 — AgX
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
