////////////////////////////////////////////////////////////////////////////////
// Воздушная перспектива: свет точки, ослабленный воздухом по пути к камере, плюс свет, рассеянный этим воздухом, —
// дымка, в которой синеют и светлеют дальние склоны (Aerial Perspective у Sky Atmosphere в UE). Одна выборка объёма,
// который каждый кадр считает SkyAtmosphere (Shaders/aerial_perspective.cs). Вызывает evaluateLighting
// (Shaders/lighting.sh) — у всех материалов с освещением
////////////////////////////////////////////////////////////////////////////////

#ifndef AERIAL_PERSPECTIVE_SH
#define AERIAL_PERSPECTIVE_SH

#include "slots.h"
#include "atmosphere_constants.h"
#include "common.vs"
#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture3D<float4>, g_aerialPerspective, SLOT_AERIAL_PERSPECTIVE );

// color — яркость точки мира position в кд/м² (без экспозиции)
float3 applyAerialPerspective( float3 color, float3 position )
{
	// Воздушной перспективы нет у уровня с панорамой вместо атмосферы (HDRIBackdrop)
	[branch] if( cb_aerialPerspectiveDistance <= 0.0f )
		return color;

	// Ячейка экрана главного вида и слой по расстоянию: слой s накоплен до D · ((s + 1) / N)²
	const float4 clip = mul( float4( position, 1.0f ), cb_viewProjectionMatrix );
	const float2 uv = clip.xy / clip.w * float2( 0.5f, -0.5f ) + 0.5f;
	const float slice = sqrt( saturate( length( position - cb_cameraPosition ) / cb_aerialPerspectiveDistance ) ) * AERIAL_PERSPECTIVE_DEPTH;
	float4 air = g_aerialPerspective.SampleLevel( g_SamplerLinearClamp, float3( uv, ( max( slice, 1.0f ) - 0.5f ) / AERIAL_PERSPECTIVE_DEPTH ), 0.0f );
	// Ближе первого слоя — к «воздуха нет» у самой камеры
	air = lerp( float4( 0.0f, 0.0f, 0.0f, 1.0f ), air, saturate( slice ) );

	// Рассеянный свет запечён для солнца 1 лк, как небо, и нормирован на яркость неба кадра: яркость — умножением на
	// освещённость от солнца и нормировку (cb_aerialPerspectiveScale)
	return color * air.a + air.rgb * cb_aerialPerspectiveScale;
}

#endif
