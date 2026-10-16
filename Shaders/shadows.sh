////////////////////////////////////////////////////////////////////////////////
// Приём тени солнца из каскадных карт (Cascaded Shadow Maps, как у directional light в UE). Карты рисует
// ShadowCascades (C++) до проходов сцены; множитель тени берёт evaluateLighting в Shaders/lighting.sh. Дальше
// Dynamic Shadow Distance — дальние тени (как Far Shadow Cascades в UE): лес — дальним каскадом на всю карту, рельеф —
// картой края тени (TerrainSunShadow); в полосе конца дистанции тень каскадов переходит в дальнюю. Горы вокруг карты
// (FarTerrain) в каскады не попадают: их тень (mountainShadow) — и ближе дистанции теней
////////////////////////////////////////////////////////////////////////////////

#ifndef SHADOWS_SH
#define SHADOWS_SH

#include "slots.h"
#include "common.vs"
#include "cloud_shadow.sh"
#include "bindless.sh"


#include "shadow_constants.h"

DM_SRV( Texture2DArray<float>, g_shadowMap, SLOT_SHADOW_MAP );
// x — запас земли над краем тени, м (отрицательный — земля в тени); y — расстояние до заслона, м; z — высота земли, м
DM_SRV( Texture2D<float4>, g_terrainSunShadow, SLOT_TERRAIN_SUN_SHADOW );
// Тень гор на карте — от заслонов за её краем: x, y — как у тени карты (земля — её z)
DM_SRV( Texture2D<float2>, g_terrainMountainShadow, SLOT_TERRAIN_MOUNTAIN_SHADOW );
// Тень на всём кольце гор — как у тени карты
DM_SRV( Texture2D<float4>, g_mountainSunShadow, SLOT_MOUNTAIN_SUN_SHADOW );
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

// Свет у края тени рельефа (TerrainSunShadow, Shaders/terrain_sun_shadow.cs): точка ниже края тени над землёй — в тени.
// clearance — запас точки над краем: запас земли текселя плюс высота точки над его землёй (не ниже её: неровности мельче
// текселя не уходят в тень); полутень — по расстоянию до заслона occluder (тень дальней горы мягче), не уже наименьшей
float terrainEdgeLight( float clearance, float occluder )
{
	const float penumbra = max( occluder * g_terrainShadow.y, g_terrainShadow.z );
	return saturate( clearance / penumbra + 0.5f );
}

// UV карты края тени карты уровня у точки мира; false — тени карты нет или точка вне карты
bool terrainShadowUV( float3 position, out float2 uv )
{
	uv = 0.0f;
	[branch] if( g_terrainShadow.x <= 0.0f )
		return false;
	uv = float2( position.x, 1.0f / g_terrainShadow.x - position.z ) * g_terrainShadow.x;
	return all( saturate( uv ) == uv );
}

// Тень гор вокруг карты (FarTerrain): у точки карты — от заслонов за её краем, у точки гор — по карте края тени кольца.
// Гор нет в каскадах — её берут и ближе дистанции теней (sunShadow, volumeShadow); без гор — 1
float mountainShadow( float3 position )
{
	float2 uv;
	[branch] if( terrainShadowUV( position, uv ) )
	{
		[branch] if( g_terrainShadow.w <= 0.0f )
			return 1.0f;
		const float ground = g_terrainSunShadow.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).z;
		const float2 edge = g_terrainMountainShadow.SampleLevel( g_SamplerLinearClamp, uv, 0.0f );
		return terrainEdgeLight( edge.x + max( position.y - ground, 0.0f ), edge.y );
	}
	[branch] if( g_mountainShadow.x > 0.0f )
	{
		const float extent = 1.0f / g_mountainShadow.x;
		const float2 local = float2( position.x - g_mountainShadow.y, extent - ( position.z - g_mountainShadow.z ) );
		const float2 mountainUV = local * g_mountainShadow.x;
		if( all( saturate( mountainUV ) == mountainUV ) )
		{
			const float4 edge = g_mountainSunShadow.SampleLevel( g_SamplerLinearClamp, mountainUV, 0.0f );
			return terrainEdgeLight( edge.x + max( position.y - edge.z, 0.0f ), edge.y );
		}
	}
	return 1.0f;
}

// Тень рельефа дальше каскадов: заслоны карты (её карта края тени) и горы вокруг неё
float terrainSunShadow( float3 position )
{
	float light = 1.0f;
	float2 uv;
	[branch] if( terrainShadowUV( position, uv ) )
	{
		const float4 edge = g_terrainSunShadow.SampleLevel( g_SamplerLinearClamp, uv, 0.0f );
		light = terrainEdgeLight( edge.x + max( position.y - edge.z, 0.0f ), edge.y );
	}
	[branch] if( light > 0.0f )
		light = min( light, mountainShadow( position ) );
	return light;
}

// Дальний каскад: лес на всю карту (импостеры). Смещения — как у каскадов, в текселях дальнего; вне карты — света
float farCascadeShadow( float3 position, float3 geometricNormal, float3 toSun, float NoL )
{
	[branch] if( g_farShadow.y <= 0.0f )
		return 1.0f;
	const float texel = g_farShadow.x;
	const float3 offset = toSun * ( g_shadowDepthBias * texel ) + geometricNormal * ( g_shadowNormalBias * texel * ( 1.0f - NoL ) );
	const float4 clip = mul( float4( position + offset, 1.0f ), g_farViewProjection );
	const float2 uv = clip.xy * float2( 0.5f, -0.5f ) + 0.5f;
	if( any( saturate( uv ) != uv ) )
		return 1.0f;
	return shadowPCF( uv, clip.z, SHADOW_CASCADE_COUNT );
}

// Дальняя тень: рельеф и лес — минимум из двух
float farShadow( float3 position, float3 geometricNormal, float3 toSun, float NoL )
{
	const float terrain = terrainSunShadow( position );
	[branch] if( terrain <= 0.0f )
		return 0.0f;
	return min( terrain, farCascadeShadow( position, geometricNormal, toSun, NoL ) );
}

// Доля дальней тени по глубине взгляда: 0 — каскады, 1 — дальняя (с конца Dynamic Shadow Distance)
float farShadowFade( float viewDepth )
{
	return saturate( ( viewDepth - g_shadowFadeStart ) / max( g_shadowDistance - g_shadowFadeStart, 1e-4f ) );
}

// Множитель прямого света солнца: 1 — освещено, 0 — в тени. toSun — направление на солнце; selfShadow — тень от самой
// поверхности, которой нет в каскадах (Surface::selfShadow): с каскадами — минимум, дальше дистанции теней — тоже
float sunShadow( float3 position, float3 geometricNormal, float3 toSun, float selfShadow = 1.0f )
{
	const float viewDepth = viewDepthOf( position );
	const float NoL = saturate( dot( geometricNormal, toSun ) );
	// К концу дистанции (Shadow Distance Fadeout Fraction) тень каскадов переходит в дальнюю (без неё — в свет)
	const float fade = farShadowFade( viewDepth );
	float shadow = 1.0f;
	[branch] if( fade < 1.0f )
	{
		const uint cascade = shadowCascade( viewDepth );
		shadow = cascadeShadow( position, geometricNormal, toSun, NoL, cascade );

		// Полоса в конце каскада (Cascade Transition Fraction): смешение со следующим, чтобы граница не была видна
		const float cascadeStart = cascade == 0 ? 0.0f : g_cascadeSplits[cascade - 1];
		const float cascadeEnd = g_cascadeSplits[cascade];
		const float band = max( ( cascadeEnd - cascadeStart ) * g_cascadeTransition, 1e-4f );
		const float blend = saturate( ( viewDepth - ( cascadeEnd - band ) ) / band );
		[branch] if( blend > 0.0f && cascade + 1 < SHADOW_CASCADE_COUNT )
			shadow = lerp( shadow, cascadeShadow( position, geometricNormal, toSun, NoL, cascade + 1 ), blend );
		// Гор в каскадах нет: их тень — и здесь
		[branch] if( shadow > 0.0f )
			shadow = min( shadow, mountainShadow( position ) );
	}
	[branch] if( fade > 0.0f )
		shadow = lerp( shadow, farShadow( position, geometricNormal, toSun, NoL ), fade );
	// Облака — и дальше дистанции теней
	return min( shadow, selfShadow ) * cloudShadow( position, toSun );
}

// Тень солнца в точке воздуха (объёмный туман, Shaders/volumetric_fog.cs): одна выборка сравнения без смещений и
// смешения каскадов — у воздуха нет поверхности, а шум выборки сглаживает накопление объёма по кадрам. toSun — на солнце
float volumeShadow( float3 position, float3 toSun )
{
	const float viewDepth = viewDepthOf( position );
	float shadow = 1.0f;
	[branch] if( g_shadowSunIndex >= 0 )
	{
		const float fade = farShadowFade( viewDepth );
		[branch] if( fade < 1.0f )
		{
			const uint cascade = shadowCascade( viewDepth );
			const float4 clip = mul( float4( position, 1.0f ), g_cascadeViewProjection[cascade] );
			const float2 uv = clip.xy * float2( 0.5f, -0.5f ) + 0.5f;
			shadow = min( g_shadowMap.SampleCmpLevelZero( g_shadowSampler, float3( uv, cascade ), clip.z ), mountainShadow( position ) );
		}
		// Дальше — рельеф по карте края тени (туман в долине в тени горы) и лес дальним каскадом
		[branch] if( fade > 0.0f )
		{
			float distant = terrainSunShadow( position );
			[branch] if( g_farShadow.y > 0.0f && distant > 0.0f )
			{
				const float4 clip = mul( float4( position, 1.0f ), g_farViewProjection );
				const float2 uv = clip.xy * float2( 0.5f, -0.5f ) + 0.5f;
				if( all( saturate( uv ) == uv ) )
					distant = min( distant, g_shadowMap.SampleCmpLevelZero( g_shadowSampler, float3( uv, SHADOW_CASCADE_COUNT ), clip.z ) );
			}
			shadow = lerp( shadow, distant, fade );
		}
	}
	return shadow * cloudShadow( position, toSun );
}

// «Show cascades»: каскады 0…3 — красный, зелёный, синий, жёлтый; дальше дистанции — пурпурный у дальних теней, без
// них — без изменений
float3 shadowCascadeTint( float3 position )
{
	const float viewDepth = viewDepthOf( position );
	if( g_showCascades == 0 || g_shadowSunIndex < 0 )
		return 1.0f;
	if( viewDepth >= g_shadowDistance )
		return g_farShadow.y > 0.0f ? float3( 1.0f, 0.3f, 1.0f ) : 1.0f;
	static const float3 colors[SHADOW_CASCADE_COUNT] = {
		float3( 1.0f, 0.3f, 0.3f ), float3( 0.3f, 1.0f, 0.3f ), float3( 0.3f, 0.3f, 1.0f ), float3( 1.0f, 1.0f, 0.3f ) };
	return colors[shadowCascade( viewDepth )];
}

#endif
