////////////////////////////////////////////////////////////////////////////////
// Автоэкспозиция, шаг 1: гистограмма log₂ яркости кадра (Auto Exposure Histogram в UE). Каждый поток берёт один
// пиксель из квадрата 2 × 2 цвета сцены, яркость переводится из pre-exposed буфера в кд/м² и раскладывается по
// 64 корзинам диапазона [Min EV100, Max EV100]: сначала в общей памяти группы, затем в буфер. Класс PostProcess
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#include "exposure.sh"

#define HISTOGRAM_BINS 64

Texture2D<float4> g_sceneColor : register( t0 );
RWByteAddressBuffer g_histogram : register( u0 );	// HISTOGRAM_BINS чисел uint

// Раскладка — PostProcess::HistogramParameters
cbuffer HistogramBuffer : register( b4 )
{
	uint2 g_sceneSize;
	float g_minLog2Luminance;	// log₂ яркости нижней границы первой корзины
	float g_log2LuminanceRange;	// log₂-ширина всех корзин
};

groupshared uint g_localBins[HISTOGRAM_BINS];

float luminance( float3 color )
{
	return dot( color, float3( 0.2126f, 0.7152f, 0.0722f ) );
}

[numthreads( 16, 16, 1 )]
void main( uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex )
{
	if( groupIndex < HISTOGRAM_BINS )
		g_localBins[groupIndex] = 0;
	GroupMemoryBarrierWithGroupSync();

	const uint2 pixel = dispatchThreadId.xy * 2;
	if( all( pixel < g_sceneSize ) )
	{
		// Чёрное (фон за пределами кадра, тени без света) в замер не входит, как в UE
		const float value = luminance( g_sceneColor.Load( int3( pixel, 0 ) ).rgb ) / max( preExposure(), 1e-30f );
		if( value > 1e-8f )
		{
			const float position = saturate( ( log2( value ) - g_minLog2Luminance ) / g_log2LuminanceRange );
			InterlockedAdd( g_localBins[min( (uint)( position * HISTOGRAM_BINS ), HISTOGRAM_BINS - 1 )], 1 );
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if( groupIndex < HISTOGRAM_BINS && g_localBins[groupIndex] > 0 )
		g_histogram.InterlockedAdd( groupIndex * 4, g_localBins[groupIndex] );
}
