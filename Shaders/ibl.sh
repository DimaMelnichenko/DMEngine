////////////////////////////////////////////////////////////////////////////////
// Освещение окружением (IBL) от неба: рассеянный свет — сферические гармоники, отражения — префильтрованный
// cubemap и таблица BRDF (split-sum). Ресурсы готовит и привязывает SkyLight (слоты PS t101…t103) — из неба атмосферы
// или панорамы; масштаб яркости — cb_skyLightScale
////////////////////////////////////////////////////////////////////////////////

#ifndef IBL_SH
#define IBL_SH

#include "slots.h"
#include "common.vs"
#include "samplers.sh"

StructuredBuffer<float4> g_irradianceSH : register( SLOT_IBL_IRRADIANCE );	// 9 коэффициентов для E(n)/π
TextureCube g_specularCube : register( SLOT_IBL_SPECULAR );				// мип m — шероховатость m / (specularMipCount − 1)
Texture2D<float2> g_brdfLut : register( SLOT_IBL_BRDF );				// u — N·V, v — шероховатость

static const float specularMipCount = 6.0f;	// SkyLight::specularMipCount

// Освещённость от неба, делённая на π: рассеянный свет ламбертовой поверхности = альбедо × результат
float3 ambientIrradiance( float3 n )
{
	float3 result = g_irradianceSH[0].rgb * 0.282095f
				  + g_irradianceSH[1].rgb * ( 0.488603f * n.y )
				  + g_irradianceSH[2].rgb * ( 0.488603f * n.z )
				  + g_irradianceSH[3].rgb * ( 0.488603f * n.x )
				  + g_irradianceSH[4].rgb * ( 1.092548f * n.x * n.y )
				  + g_irradianceSH[5].rgb * ( 1.092548f * n.y * n.z )
				  + g_irradianceSH[6].rgb * ( 0.315392f * ( 3.0f * n.z * n.z - 1.0f ) )
				  + g_irradianceSH[7].rgb * ( 1.092548f * n.x * n.z )
				  + g_irradianceSH[8].rgb * ( 0.546274f * ( n.x * n.x - n.y * n.y ) );
	// Атмосфера запечена для солнца освещённостью 1 лк (небо линейно по солнцу), панорама — в своих единицах
	return max( result, 0.0f ) * cb_skyLightScale;
}

// Отражённый свет неба в направлении r, размытый по шероховатости
float3 ambientSpecular( float3 r, float roughness )
{
	return g_specularCube.SampleLevel( g_SamplerLinearClamp, r, roughness * ( specularMipCount - 1.0f ) ).rgb * cb_skyLightScale;
}

// Масштаб и сдвиг F0 для отражений окружения: отражение = ambientSpecular × (F0·A + B)
float2 environmentBRDF( float NoV, float roughness )
{
	return g_brdfLut.SampleLevel( g_SamplerLinearClamp, float2( NoV, roughness ), 0.0f );
}

#endif
