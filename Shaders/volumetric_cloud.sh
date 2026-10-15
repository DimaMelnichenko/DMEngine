////////////////////////////////////////////////////////////////////////////////
// Облака — общее для проходов VolumetricCloud: параметры слоя и плотность в точке (A. Schneider, «The Real-time
// Volumetric Cloudscapes of Horizon Zero Dawn», SIGGRAPH 2015). Форма — шум Перлина — Уорли и Уорли трёх частот
// (g_cloudShape, 128³), края размываются мелким шумом Уорли (g_cloudDetail, 32³), где облака — по карте погоды
// (g_cloudWeather: доля покрытия и тип облака) и общему покрытию. Шумы и карта — Shaders/cloud_noise.cs
////////////////////////////////////////////////////////////////////////////////

#ifndef VOLUMETRIC_CLOUD_SH
#define VOLUMETRIC_CLOUD_SH

#include "slots.h"
#include "samplers.sh"
#include "bindless.sh"

#include "volumetric_cloud_constants.h"

DM_SRV( Texture3D<float4>, g_cloudShape, 6 );
DM_SRV( Texture3D<float4>, g_cloudDetail, 7 );
DM_SRV( Texture2D<float2>, g_cloudWeather, 8 );

float cloudRemap( float value, float low, float high, float newLow, float newHigh )
{
	return newLow + ( value - low ) * ( newHigh - newLow ) / ( high - low );
}

// Плотность (коэффициент ослабления, 1/м) в точке мира world на высоте height над землёй. detail — с мелким шумом
// по краям: он съедает тонкие и рыхлые части, поэтому всё, что должно совпасть с видимыми облаками (карта тени), — с ним;
// без него (дешевле) — дальние шаги луча к солнцу внутри облака
float cloudDensity( float3 world, float height, bool detail )
{
	const float h = ( height - g_layerBottom ) / ( g_layerTop - g_layerBottom );
	if( h <= 0.0f || h >= 1.0f )
		return 0.0f;

	const float2 position = world.xz + g_windOffset;
	const float2 weather = g_cloudWeather.SampleLevel( g_SamplerLinearWrap, position * g_weatherScale, 0.0f );
	// Покрытие — общее с местными колебаниями по карте погоды: где-то просветы, где-то гуще
	const float coverage = saturate( g_coverage + ( weather.x - 0.5f ) * 0.8f );
	if( coverage <= 0.0f )
		return 0.0f;

	// Профиль по высоте: основание плоское, верх — у слоистых ниже (тип 0), у кучевых — до верха слоя (тип 1)
	const float top = lerp( 0.35f, 1.0f, weather.y );
	const float gradient = saturate( h / 0.08f ) * saturate( ( top - h ) / ( top * 0.45f ) );
	if( gradient <= 0.0f )
		return 0.0f;

	const float3 uvw = float3( position.x, height, position.y ) * g_shapeScale;
	const float4 shape = g_cloudShape.SampleLevel( g_SamplerLinearWrap, uvw, 0.0f );
	const float lowFrequency = dot( shape.gba, float3( 0.625f, 0.25f, 0.125f ) );
	float base = saturate( cloudRemap( shape.r, lowFrequency - 1.0f, 1.0f, 0.0f, 1.0f ) ) * gradient;
	// Покрытие отрезает слабые места шума: чем меньше покрытие, тем меньше остаётся облаков
	base = saturate( cloudRemap( base, 1.0f - coverage, 1.0f, 0.0f, 1.0f ) ) * coverage;

	[branch] if( detail && base > 0.0f )
	{
		const float3 noise = g_cloudDetail.SampleLevel( g_SamplerLinearWrap, float3( position.x, height, position.y ) * g_detailScale, 0.0f ).rgb;
		const float fbm = dot( noise, float3( 0.625f, 0.25f, 0.125f ) );
		// Внизу края рваные, вверху — клубы
		const float modifier = lerp( fbm, 1.0f - fbm, saturate( h * 5.0f ) );
		base = saturate( cloudRemap( base, modifier * 0.35f, 1.0f, 0.0f, 1.0f ) );
	}
	return base * g_cloudDensity;
}

#endif
