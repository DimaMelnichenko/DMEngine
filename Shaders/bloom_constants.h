////////////////////////////////////////////////////////////////////////////////
// Константы проходов bloom (PostProcess, Shaders/bloom_downsample.ps, bloom_upsample.ps). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_BLOOM_CONSTANTS_H
#define DM_BLOOM_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( BloomBuffer, SLOT_CB_PASS )
{
	DM_FIELD( float2, sourceTexelSize );	// 1 / размер источника
	DM_FIELD( float, threshold );			// Bloom Threshold, яркость после экспозиции; < 0 — без порога
	DM_FIELD( int, firstPass );			// 1 — источник — цвет сцены
	DM_FIELD( float, radius );			// для увеличения: радиус тента в текселях источника
	DM_FIELD( float, weight );			// для увеличения: вес добавляемого уровня
	DM_FIELD( float2, padding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
