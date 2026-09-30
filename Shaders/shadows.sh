////////////////////////////////////////////////////////////////////////////////
// Приём тени солнца из каскадных карт (Cascaded Shadow Maps, как у directional light в UE). Карты рисует
// ShadowCascades (C++) до проходов сцены; множитель тени берёт evaluateLighting в Shaders/lighting.sh
////////////////////////////////////////////////////////////////////////////////

#ifndef SHADOWS_SH
#define SHADOWS_SH

#include "slots.h"
#include "common.vs"
#include "bindless.sh"

#define SHADOW_CASCADE_COUNT 4

// Раскладка — ShadowCascades::ShaderShadowConstants
cbuffer ShadowConstants : register( SLOT_CB_SHADOW )
{
	matrix g_cascadeViewProjection[SHADOW_CASCADE_COUNT];
	float4 g_cascadeSplits;		// дальняя граница каскада по глубине взгляда, м
	float4 g_cascadeTexelSize;	// размер текселя каскада в мире, м
	float  g_cascadeTransition;	// доля каскада в его конце, где он смешивается со следующим
	float  g_shadowFadeStart;	// отсюда тень плавно уходит…
	float  g_shadowDistance;	// …и здесь заканчивается (Dynamic Shadow Distance), м
	float  g_shadowNormalBias;	// сдвиг точки по нормали, в текселях каскада
	int    g_shadowSunIndex;	// индекс солнца в g_lights, −1 — теней нет
	int    g_showCascades;		// 1 — подкрасить каскады
	float  g_shadowMapSize;		// размер среза, текселей
	float  g_shadowDepthBias;	// сдвиг точки к солнцу, в текселях каскада
};

DM_SRV( Texture2DArray<float>, g_shadowMap, SLOT_SHADOW_MAP );
SamplerComparisonState g_shadowSampler : register( SLOT_SAMPLER_SHADOW );

// Фильтр 5 × 5 текселей девятью выборками сравнения с билинейной фильтрацией (PCF 2×2 в каждой): веса и смещения
// выборок подобраны так, что вместе они дают треугольное ядро — мягче и без ступенек сетки 3 × 3 той же ценой
// (I. Castaño, «Shadow Mapping Summary», 2013; так же в The Witness и примерах MJP)
float shadowPCF( float2 uv, float depth, uint cascade )
{
	const float2 texel = uv * g_shadowMapSize;
	const float2 base = floor( texel + 0.5f );
	const float s = texel.x + 0.5f - base.x;
	const float t = texel.y + 0.5f - base.y;
	const float2 baseUV = ( base - 0.5f ) / g_shadowMapSize;

	const float3 uw = float3( 4.0f - 3.0f * s, 7.0f, 1.0f + 3.0f * s );
	const float3 u = float3( ( 3.0f - 2.0f * s ) / uw.x - 2.0f, ( 3.0f + s ) / uw.y, s / uw.z + 2.0f );
	const float3 vw = float3( 4.0f - 3.0f * t, 7.0f, 1.0f + 3.0f * t );
	const float3 v = float3( ( 3.0f - 2.0f * t ) / vw.x - 2.0f, ( 3.0f + t ) / vw.y, t / vw.z + 2.0f );

	float sum = 0.0f;
	[unroll] for( int y = 0; y < 3; ++y )
	{
		[unroll] for( int x = 0; x < 3; ++x )
		{
			const float2 sampleUV = baseUV + float2( u[x], v[y] ) / g_shadowMapSize;
			sum += uw[x] * vw[y] * g_shadowMap.SampleCmpLevelZero( g_shadowSampler, float3( sampleUV, cascade ), depth );
		}
	}
	return sum / 144.0f;
}

// Тень в одном каскаде. Точка сдвигается в долях текселя каскада, так что смещение растёт вместе с текселем:
// к солнцу (Shadow Bias в UE) — ядро фильтра захватывает соседние грани рельефа и изгиб поверхности, и по
// геометрической нормали (Normal Bias), тем больше, чем положе падает свет. Так поверхность не затеняет сама
// себя (acne), а тень не отрывается от основания объекта, как при большом смещении глубины в растеризаторе
float cascadeShadow( float3 position, float3 geometricNormal, float3 toSun, float NoL, uint cascade )
{
	const float texel = g_cascadeTexelSize[cascade];
	const float3 offset = toSun * ( g_shadowDepthBias * texel ) + geometricNormal * ( g_shadowNormalBias * texel * ( 1.0f - NoL ) );
	// Проекция ортографическая: w = 1
	const float4 clip = mul( float4( position + offset, 1.0f ), g_cascadeViewProjection[cascade] );
	const float2 uv = clip.xy * float2( 0.5f, -0.5f ) + 0.5f;
	return shadowPCF( uv, clip.z, cascade );
}

// Каскад по глубине вдоль взгляда
uint shadowCascade( float viewDepth )
{
	uint cascade = 0;
	[unroll] for( uint c = 0; c < SHADOW_CASCADE_COUNT - 1; ++c )
		cascade += viewDepth >= g_cascadeSplits[c] ? 1 : 0;
	return cascade;
}

float viewDepthOf( float3 position )
{
	return dot( position - cb_cameraPosition, cb_viewDirection );
}

// Множитель прямого света солнца: 1 — освещено, 0 — в тени. toSun — направление на солнце
float sunShadow( float3 position, float3 geometricNormal, float3 toSun )
{
	const float viewDepth = viewDepthOf( position );
	float shadow = 1.0f;
	[branch] if( viewDepth < g_shadowDistance )
	{
		const float NoL = saturate( dot( geometricNormal, toSun ) );
		const uint cascade = shadowCascade( viewDepth );
		shadow = cascadeShadow( position, geometricNormal, toSun, NoL, cascade );

		// Полоса в конце каскада (Cascade Transition Fraction): смешение со следующим, чтобы граница не была видна
		const float cascadeStart = cascade == 0 ? 0.0f : g_cascadeSplits[cascade - 1];
		const float cascadeEnd = g_cascadeSplits[cascade];
		const float band = max( ( cascadeEnd - cascadeStart ) * g_cascadeTransition, 1e-4f );
		const float blend = saturate( ( viewDepth - ( cascadeEnd - band ) ) / band );
		[branch] if( blend > 0.0f && cascade + 1 < SHADOW_CASCADE_COUNT )
			shadow = lerp( shadow, cascadeShadow( position, geometricNormal, toSun, NoL, cascade + 1 ), blend );

		// К концу дистанции (Shadow Distance Fadeout Fraction) тень плавно уходит
		const float fade = saturate( ( viewDepth - g_shadowFadeStart ) / max( g_shadowDistance - g_shadowFadeStart, 1e-4f ) );
		shadow = lerp( shadow, 1.0f, fade );
	}
	return shadow;
}

// «Show cascades»: каскады 0…3 — красный, зелёный, синий, жёлтый; дальше дистанции — без изменений
float3 shadowCascadeTint( float3 position )
{
	const float viewDepth = viewDepthOf( position );
	if( g_showCascades == 0 || g_shadowSunIndex < 0 || viewDepth >= g_shadowDistance )
		return 1.0f;
	static const float3 colors[SHADOW_CASCADE_COUNT] = {
		float3( 1.0f, 0.3f, 0.3f ), float3( 0.3f, 1.0f, 0.3f ), float3( 0.3f, 0.3f, 1.0f ), float3( 1.0f, 1.0f, 0.3f ) };
	return colors[shadowCascade( viewDepth )];
}

#endif
