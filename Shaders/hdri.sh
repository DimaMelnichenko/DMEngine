////////////////////////////////////////////////////////////////////////////////
// HDRI-панорама — равнопромежуточная проекция (equirectangular): u — долгота, v — от зенита к надиру. Долгота 0
// (центр панорамы, u = 0,5) — на +Z, 90° — на +X, как Yaw у источника; g_rotation поворачивает панораму вокруг
// вертикали. Класс HDRIBackdrop: cubemap для освещения окружением (hdri_cube.ps) и фон (hdri_background.ps)
////////////////////////////////////////////////////////////////////////////////

#ifndef HDRI_SH
#define HDRI_SH

#include "slots.h"
#include "samplers.sh"
#include "cubemap.sh"

// Раскладка — HDRIBackdrop::Parameters
cbuffer HDRIParameters : register( SLOT_CB_PASS )
{
	int   g_face;			// грань cubemap освещения: 0…5 — +X, −X, +Y, −Y, +Z, −Z
	float g_rotation;		// поворот панорамы вокруг вертикали, радианы
	float g_maxLuminance;	// срез яркости для освещения окружением, в единицах панорамы; 0 — без среза
	float g_lod;			// мип панорамы для граней cubemap
};

Texture2D<float4> g_panorama : register( t0 );

float luminance( float3 color )
{
	return dot( color, float3( 0.2126f, 0.7152f, 0.0722f ) );
}

// Яркость панорамы в направлении direction (мир), мип lod. По долготе панорама замкнута, по широте — нет: v не
// доходит до края мипа, иначе фильтр смешал бы зенит с надиром
float3 samplePanorama( float3 direction, float lod )
{
	uint width, height, levels;
	g_panorama.GetDimensions( (uint)lod, width, height, levels );
	const float longitude = atan2( direction.x, direction.z ) - g_rotation;
	const float latitude = acos( clamp( direction.y, -1.0f, 1.0f ) );
	const float halfTexel = 0.5f / height;
	const float2 uv = float2( frac( longitude / ( 2.0f * cubemapPi ) + 0.5f ),
							  clamp( latitude / cubemapPi, halfTexel, 1.0f - halfTexel ) );
	return g_panorama.SampleLevel( g_SamplerLinearWrap, uv, lod ).rgb;
}

#endif
