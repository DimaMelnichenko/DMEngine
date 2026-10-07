////////////////////////////////////////////////////////////////////////////////
// Туман по высоте (Exponential Height Fog в UE) и объёмный туман (Volumetric Fog). Плотность — два слоя: ниже своей
// высоты слой ровный, выше редеет по экспоненте (дымка над долиной и туман, налитый в низины). Ближе дальности объёма
// туман берётся из объёма над экраном (VolumetricFog, Shaders/volumetric_fog.cs: свет солнца с каскадной тенью, неба и
// ламп — лучи в дымке), дальше — по формуле вдоль луча, со светом солнца и неба без теней. Параметры — cb_fog* в
// Shaders/common.vs. Вызывает applyFogging — evaluateLighting (Shaders/lighting.sh), фон неба, вода, частицы
////////////////////////////////////////////////////////////////////////////////

#ifndef HEIGHT_FOG_SH
#define HEIGHT_FOG_SH

#include "slots.h"
#include "fog_constants.h"
#include "common.vs"
#include "samplers.sh"
#include "lights.sh"
#include "ibl.sh"
#include "shadows.sh"
#include "aerial_perspective.sh"
#include "bindless.sh"

DM_SRV( Texture3D<float4>, g_volumetricFog, SLOT_VOLUMETRIC_FOG );

static const float fogPi = 3.14159265f;

// Плотность слоя (коэффициент ослабления, 1/м) на высоте y: layer — плотность, высота, спад, —
float fogLayerDensity( float4 layer, float y )
{
	return layer.x * exp( -layer.z * max( y - layer.y, 0.0f ) );
}

float fogDensity( float y )
{
	return fogLayerDensity( cb_fogLayer0, y ) + fogLayerDensity( cb_fogLayer1, y );
}

// Оптическая толщина слоя на отрезке длиной length между высотами y0 и y1: ниже высоты слоя — плотность × путь,
// выше — интеграл экспоненты по высоте
float fogLayerOpticalDepth( float4 layer, float y0, float y1, float length )
{
	const float low = min( y0, y1 );
	const float high = max( y0, y1 );
	const float rise = high - low;
	[branch] if( rise < 0.01f )
		return fogLayerDensity( layer, 0.5f * ( y0 + y1 ) ) * length;
	const float falloff = max( layer.z, 1e-4f );
	const float below = saturate( ( layer.y - low ) / rise );
	const float aboveStart = max( low, layer.y );
	const float above = ( exp( -falloff * ( aboveStart - layer.y ) ) - exp( -falloff * max( high - layer.y, 0.0f ) ) ) / ( falloff * rise );
	return layer.x * length * ( below + above );
}

float fogOpticalDepth( float y0, float y1, float length )
{
	return fogLayerOpticalDepth( cb_fogLayer0, y0, y1, length ) + fogLayerOpticalDepth( cb_fogLayer1, y0, y1, length );
}

// Фазовая функция Хеньи — Гринстейна: g > 0 — рассеяние вперёд (ореол вокруг солнца), cosAngle — между направлением
// света и направлением к камере
float henyeyGreenstein( float cosAngle, float g )
{
	const float denominator = 1.0f + g * g - 2.0f * g * cosAngle;
	return ( 1.0f - g * g ) / ( 4.0f * fogPi * denominator * sqrt( denominator ) );
}

// Слои сетки объёма по глубине взгляда (как GridZParams в UE): слой = log₂(z · B + O) · S, cb_fogGridZ = (B, O, S)
float fogSliceOf( float viewDepth )
{
	return log2( max( viewDepth * cb_fogGridZ.x + cb_fogGridZ.y, 1e-6f ) ) * cb_fogGridZ.z;
}

float fogSliceDepth( float slice )
{
	return ( exp2( slice / cb_fogGridZ.z ) - cb_fogGridZ.y ) / cb_fogGridZ.x;
}

// Свет, рассеянный в сторону камеры единицей альбедо, без теней: солнце (или луна) с фазовой функцией и небо
// со всех сторон. direction — от камеры
float3 fogInscattering( float3 direction )
{
	float3 light = ambientAverageRadiance();
	[branch] if( g_shadowSunIndex >= 0 )
	{
		const Light sun = g_lights[g_shadowSunIndex];
		light += sun.color * henyeyGreenstein( dot( sun.direction, -direction ), cb_fogLayer1.w );
	}
	return light * cb_fogAlbedo;
}

// color — яркость точки мира position, кд/м², без экспозиции: туман между точкой и камерой
float3 applyHeightFog( float3 color, float3 position )
{
	// Тумана нет у уровня без строки ExponentialHeightFog
	[branch] if( cb_fogScale <= 0.0f )
		return color;

	const float3 offset = position - cb_cameraPosition;
	const float distance = max( length( offset ), 1e-4f );
	const float viewDepth = dot( offset, cb_viewDirection );

	// Дальше объёма — по формуле вдоль луча от конца объёма до точки (дальняя часть пути — первой)
	[branch] if( viewDepth > cb_fogVolumeDistance )
	{
		const float3 direction = offset / distance;
		const float start = distance * cb_fogVolumeDistance / viewDepth;
		const float transmittance = exp( -fogOpticalDepth( cb_cameraPosition.y + direction.y * start, position.y, distance - start ) );
		color = color * transmittance + fogInscattering( direction ) * ( 1.0f - transmittance );
	}

	// Ближняя часть — из объёма: слой s хранит накопленное к концу слоя, у камеры — к «тумана нет»
	[branch] if( cb_fogVolumeDistance > 0.0f )
	{
		const float4 clip = mul( float4( position, 1.0f ), cb_viewProjectionMatrix );
		const float2 uv = clip.xy / clip.w * float2( 0.5f, -0.5f ) + 0.5f;
		const float slice = fogSliceOf( clamp( viewDepth, 0.0f, cb_fogVolumeDistance ) );
		float4 fog = g_volumetricFog.SampleLevel( g_SamplerLinearClamp, float3( uv, ( max( slice, 1.0f ) - 0.5f ) / VOLUMETRIC_FOG_DEPTH ), 0.0f );
		fog = lerp( float4( 0.0f, 0.0f, 0.0f, 1.0f ), fog, saturate( slice ) );
		// Объём хранит свет, делённый на cb_fogScale (половинной точности хватает и днём, и ночью)
		color = color * fog.a + fog.rgb * cb_fogScale;
	}
	return color;
}

// Фон неба в направлении direction (от камеры) — за туманом до дальней плоскости; воздух в небе уже есть
float3 applyHeightFogToSky( float3 luminance, float3 direction )
{
	return applyHeightFog( luminance, cb_cameraPosition + direction * cb_fogLayer0.w );
}

// Всё, что лежит между точкой и камерой: туман, затем воздух (Shaders/aerial_perspective.sh)
float3 applyFogging( float3 color, float3 position )
{
	return applyAerialPerspective( applyHeightFog( color, position ), position );
}

#endif
