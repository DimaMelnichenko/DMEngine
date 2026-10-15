////////////////////////////////////////////////////////////////////////////////
// Экранное затенение окружением (GTAO, AmbientOcclusion, Shaders/gtao.cs) в пикселе материала: из половины кадра —
// билинейно по четырём текселям с весами по глубине: тексель другой поверхности (край объекта, прозрачное перед
// непрозрачным) не смешивается, а если своих нет — затенения нет. Гасит только освещение окружением
// (Surface::screenOcclusion в Shaders/lighting.sh), прямой свет — тени. Константы — cb_ambientOcclusion кадра
////////////////////////////////////////////////////////////////////////////////

#ifndef AMBIENT_OCCLUSION_SH
#define AMBIENT_OCCLUSION_SH

#include "slots.h"
#include "common.vs"
#include "bindless.sh"

// x — видимость 0…1, y — глубина вида точки, м
DM_SRV( Texture2D<float2>, g_ambientOcclusion, SLOT_AMBIENT_OCCLUSION );

// pixel — SV_Position.xy пикселя (центр), worldPosition — его точка в мире; 1 — не затенено
float screenAmbientOcclusion( float2 pixel, float3 worldPosition )
{
	[branch] if( cb_ambientOcclusion.x <= 0.0f )
		return 1.0f;
	const float depth = dot( worldPosition - cb_cameraPosition, cb_viewDirection );
	// Тексель половины кадра i — пиксель полного (2i, 2i): его центр 2i + 0,5
	const float2 coord = ( pixel - 0.5f ) * 0.5f;
	const float2 base = floor( coord );
	const float2 f = coord - base;
	const int2 last = int2( cb_ambientOcclusion.yz ) - 1;
	float sum = 0.0f;
	float weightSum = 0.0f;
	[unroll] for( int i = 0; i < 4; ++i )
	{
		const int2 offset = int2( i & 1, i >> 1 );
		const float2 value = g_ambientOcclusion.Load( int3( clamp( int2( base ) + offset, int2( 0, 0 ), last ), 0 ) );
		const float bilinear = ( offset.x ? f.x : 1.0f - f.x ) * ( offset.y ? f.y : 1.0f - f.y );
		const float weight = bilinear * saturate( 1.0f - abs( value.y - depth ) / ( depth * 0.05f + 0.05f ) );
		sum += value.x * weight;
		weightSum += weight;
	}
	if( weightSum < 1e-3f )
		return 1.0f;
	return lerp( 1.0f, sum / weightSum, cb_ambientOcclusion.x );
}

#endif
