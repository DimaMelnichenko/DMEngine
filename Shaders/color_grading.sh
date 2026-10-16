////////////////////////////////////////////////////////////////////////////////
// Таблица цветокоррекции и тонмаппинга: вход ячейки (color_grading_lut.cs) и выборка (tonemap.ps). Ячейка хранит цвет
// экрана в кодировке sRGB: в ней интерполяция ровнее для глаза, чем в линейном цвете (тени не «ступенятся»)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_COLOR_GRADING_SH
#define DM_COLOR_GRADING_SH

#include "color_grading_lut.h"

// Линейный цвет ячейки по каждому каналу: первая — чёрный, дальше — 2^(min … max) равными шагами по log₂
float3 colorGradingCellColor( uint3 cell )
{
	const float3 t = float3( cell ) / ( COLOR_GRADING_LUT_SIZE - 1 );
	const float3 color = exp2( lerp( COLOR_GRADING_LOG2_MIN, COLOR_GRADING_LOG2_MAX, t ) );
	return select( cell == 0, float3( 0.0f, 0.0f, 0.0f ), color );
}

// Координата выборки по линейному цвету: центры ячеек — от первой до последней, ниже 2^min — первая (чёрный)
float3 colorGradingCoordinate( float3 color )
{
	const float3 t = saturate( ( log2( max( color, 1e-30f ) ) - COLOR_GRADING_LOG2_MIN ) /
							   ( COLOR_GRADING_LOG2_MAX - COLOR_GRADING_LOG2_MIN ) );
	return ( t * ( COLOR_GRADING_LUT_SIZE - 1 ) + 0.5f ) / COLOR_GRADING_LUT_SIZE;
}

// Кодировка sRGB (IEC 61966-2-1) и обратно
float3 linearToSRGB( float3 color )
{
	color = saturate( color );
	return select( color <= 0.0031308f, color * 12.92f, 1.055f * pow( color, 1.0f / 2.4f ) - 0.055f );
}

float3 sRGBToLinear( float3 color )
{
	return select( color <= 0.04045f, color / 12.92f, pow( ( color + 0.055f ) / 1.055f, 2.4f ) );
}

#endif
