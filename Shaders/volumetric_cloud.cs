////////////////////////////////////////////////////////////////////////////////
// Облака — каждый кадр (VolumetricCloud), как Volumetric Cloud в UE5:
// - mainTrace — луч главного вида через слой облаков (половина кадра): шаги по плотности (Shaders/volumetric_cloud.sh),
//   в каждой точке — свет светила через облако к нему (шесть шагов) с фазовой функцией двух лепестков и приближением
//   многократного рассеяния октавами (S. Hillaire, «Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite»,
//   SIGGRAPH 2016), свет неба сверху и земли снизу; воздух между камерой и облаком — по модели неба (integrateScattering).
//   Начало луча сдвигается по кадрам, результат смешивается с прошлым кадром, перенесённым по направлению. Пишет свет
//   облаков (в единицах запекания неба, делённый на нормировку) и пропускание — их кладёт поверх неба
//   Shaders/cloud_composite.ps;
// - mainShadow — карта тени облаков: на плоскости в середине слоя вокруг камеры пропускание слоя вдоль луча к светилу
//   (Shaders/cloud_shadow.sh)
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "atmosphere.sh"
#include "ibl.sh"
#include "volumetric_cloud.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float4>, g_cloudHistory, 0 );
DM_UAV( RWTexture2D<float4>, g_cloudsOut, 0 );
DM_UAV( RWTexture2D<float>, g_cloudShadowOut, 0 );

static const float cloudPi = 3.14159265f;
// Фазовая функция облака — два лепестка Хеньи — Гринстейна (Phase G, G2, Blend у Volumetric Cloud в UE)
static const float phaseForward = 0.5f;
static const float phaseBackward = -0.5f;
static const float phaseBlend = 0.5f;
// Многократное рассеяние октавами: ослабление, рассеяние и анизотропия каждой следующей — × эти доли
static const int scatteringOctaves = 4;
static const float octaveExtinction = 0.3f;
static const float octaveScattering = 0.6f;
static const float octavePhase = 0.5f;
// Затенение света неба: выборка плотности на этой доле толщины слоя выше точки, оптическая толщина × доля
static const float skyOcclusionStep = 0.15f;
static const float skyOcclusion = 0.5f;
// Шаги луча к светилу — доли толщины слоя (в сумме ~1)
static const float lightSteps[6] = { 0.02f, 0.04f, 0.07f, 0.12f, 0.25f, 0.5f };

float cloudHenyeyGreenstein( float cosAngle, float g )
{
	const float denominator = 1.0f + g * g - 2.0f * g * cosAngle;
	return ( 1.0f - g * g ) / ( 4.0f * cloudPi * denominator * sqrt( denominator ) );
}

float cloudPhase( float cosAngle, float scale )
{
	return lerp( cloudHenyeyGreenstein( cosAngle, phaseForward * scale ), cloudHenyeyGreenstein( cosAngle, phaseBackward * scale ), phaseBlend );
}

// Высота точки над землёй с кривизной планеты: центр планеты — под камерой
float cloudHeight( float3 world )
{
	const float3 fromCenter = float3( world.x - cb_cameraPosition.x, world.y + planetRadius, world.z - cb_cameraPosition.z );
	return length( fromCenter ) - planetRadius;
}

// Оптическая толщина от точки к светилу через слой
float lightOpticalDepth( float3 world )
{
	const float thickness = g_layerTop - g_layerBottom;
	float depth = 0.0f;
	float travelled = 0.0f;
	[unroll] for( int i = 0; i < 6; ++i )
	{
		const float step = lightSteps[i] * thickness;
		const float3 position = world + g_lightDirection * ( travelled + step * 0.5f );
		depth += cloudDensity( position, cloudHeight( position ), i < 2 ) * step;
		travelled += step;
	}
	return depth;
}

// Шум со сдвигом по кадрам (interleaved gradient noise): начала соседних лучей и кадров разнесены
float interleavedNoise( uint2 pixel, uint frame )
{
	const float2 p = pixel + 5.588238f * ( frame % 64 );
	return frac( 52.9829189f * frac( dot( p, float2( 0.06711056f, 0.00583715f ) ) ) );
}

[numthreads( 8, 8, 1 )]
void mainTrace( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_traceSize ) )
		return;

	const float2 uv = ( id.xy + 0.5f ) / g_traceSize;
	const float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
	const float3 viewDirection = float3( ndc.x / cb_projectionMatrix[0][0], ndc.y / cb_projectionMatrix[1][1], 1.0f );
	const float3 direction = normalize( mul( viewDirection, (float3x3)cb_viewInverseMatrix ) );

	// Отрезок луча в слое: камера под слоем, в нём или над ним; луч в землю облаков не видит
	const float3 origin = float3( 0.0f, planetRadius + cb_cameraPosition.y, 0.0f );
	const float2 inner = raySphere( origin, direction, planetRadius + g_layerBottom );
	const float2 outer = raySphere( origin, direction, planetRadius + g_layerTop );
	const float2 ground = raySphere( origin, direction, planetRadius );
	float start = 0.0f;
	float end = 0.0f;
	if( cb_cameraPosition.y < g_layerBottom )
	{
		start = inner.y;
		end = outer.y;
		if( ground.x > 0.0f )
			end = 0.0f;
	}
	else if( cb_cameraPosition.y < g_layerTop )
		end = inner.x > 0.0f ? inner.x : outer.y;
	else
	{
		start = max( outer.x, 0.0f );
		end = inner.x > 0.0f ? inner.x : outer.y;
		if( outer.x < 0.0f )
			end = 0.0f;
	}
	end = min( end, g_maxDistance );

	float3 luminance = 0.0f;
	float transmittance = 1.0f;
	[branch] if( end > start )
	{
		// Шагов — по длине отрезка: у зенита ~30, к горизонту до 96 (дальние облака — крупнее шаг)
		const int steps = clamp( (int)ceil( ( end - start ) / 80.0f ), 24, 96 );
		const float stepLength = ( end - start ) / steps;
		const float cosAngle = dot( direction, g_lightDirection );
		float phases[scatteringOctaves];
		[unroll] for( int o = 0; o < scatteringOctaves; ++o )
			phases[o] = cloudPhase( cosAngle, pow( octavePhase, o ) );

		// Свет неба: сверху — по нормали вверх, снизу — отражённый землёй (по нормали вниз); в единицах запекания
		const float skyScale = max( cb_skyScale, 1e-6f );
		const float3 ambientTop = ambientIrradiance( float3( 0.0f, 1.0f, 0.0f ) ) / skyScale;
		const float3 ambientBottom = ambientIrradiance( float3( 0.0f, -1.0f, 0.0f ) ) / skyScale;

		float t = start + stepLength * interleavedNoise( id.xy, g_frameIndex );
		float distanceSum = 0.0f;
		float weightSum = 0.0f;
		[loop] for( int i = 0; i < steps && transmittance > 0.01f; ++i, t += stepLength )
		{
			const float3 world = cb_cameraPosition + direction * t;
			const float height = cloudHeight( world );
			const float density = cloudDensity( world, height, true );
			[branch] if( density <= 0.0f )
				continue;

			// Свет светила у точки: над атмосферой × пропускание атмосферы к нему с высоты точки
			const float3 up = normalize( float3( world.x - cb_cameraPosition.x, world.y + planetRadius, world.z - cb_cameraPosition.z ) );
			const float3 lightColor = g_lightColor * transmittanceToTop( height + observerAltitude, dot( up, g_lightDirection ) );
			const float opticalDepth = lightOpticalDepth( world );
			float lightAmount = 0.0f;
			[unroll] for( int o = 0; o < scatteringOctaves; ++o )
				lightAmount += pow( octaveScattering, o ) * phases[o] * exp( -pow( octaveExtinction, o ) * opticalDepth );

			// Свет неба внутри облака гаснет под толщей над точкой (одна выборка плотности выше по вертикали; доля
			// skyOcclusion — многократное рассеяние пропускает больше, чем прямой луч): у основания облака оно серое
			const float h = saturate( ( height - g_layerBottom ) / ( g_layerTop - g_layerBottom ) );
			const float aboveStep = skyOcclusionStep * ( g_layerTop - g_layerBottom );
			const float3 abovePoint = world + up * aboveStep;
			const float above = cloudDensity( abovePoint, cloudHeight( abovePoint ), false ) * aboveStep;
			const float3 ambient = lerp( ambientBottom, ambientTop, h ) * exp( -skyOcclusion * above );
			const float3 scattered = g_cloudAlbedo * ( lightColor * lightAmount + ambient );

			// Отрезок с ослаблением внутри (Hillaire 2015)
			const float stepTransmittance = exp( -density * stepLength );
			luminance += transmittance * scattered * ( 1.0f - stepTransmittance );
			const float opacity = transmittance * ( 1.0f - stepTransmittance );
			distanceSum += t * opacity;
			weightSum += opacity;
			transmittance *= stepTransmittance;
		}

		// Воздух между камерой и облаком (на средней по непрозрачности дальности): облако синеет и тонет в дымке
		[branch] if( weightSum > 0.0f )
		{
			float3 air = 0.0f;
			float3 airTransmittance = 1.0f;
			const float3 observer = float3( 0.0f, planetRadius + observerAltitude, 0.0f );
			integrateScattering( observer, direction, 0.0f, distanceSum / weightSum, 4, g_sunColor, g_moonColor, air, airTransmittance );
			luminance = luminance * airTransmittance + air * g_skyIntensity * ( 1.0f - transmittance );
		}
	}

	float4 result = float4( luminance * g_outputScale, transmittance );
	// Прошлый кадр — по тому же направлению: облака далеко, сдвиг камеры их не смещает
	[branch] if( g_historyWeight > 0.0f )
	{
		const float4 clip = mul( float4( direction, 0.0f ), g_previousViewProjection );
		if( clip.w > 0.0f )
		{
			const float2 previous = clip.xy / clip.w * float2( 0.5f, -0.5f ) + 0.5f;
			if( all( previous >= 0.0f ) && all( previous <= 1.0f ) )
				result = lerp( result, g_cloudHistory.SampleLevel( g_SamplerLinearClamp, previous, 0.0f ), g_historyWeight );
		}
	}
	g_cloudsOut[id.xy] = result;
}

[numthreads( 8, 8, 1 )]
void mainShadow( uint3 id : SV_DispatchThreadID )
{
	uint width, height;
	g_cloudShadowOut.GetDimensions( width, height );
	if( any( id.xy >= uint2( width, height ) ) )
		return;

	float shadow = 1.0f;
	[branch] if( g_lightDirection.y > 0.0f && g_shadowStrength > 0.0f )
	{
		// Точка плоскости в середине слоя и отрезок луча к светилу через слой (без кривизны: карта — несколько км)
		const float middle = 0.5f * ( g_layerBottom + g_layerTop );
		const float2 plane = g_shadowOrigin + ( id.xy + 0.5f ) / float2( width, height ) * g_shadowSize;
		const float lightY = max( g_lightDirection.y, 0.05f );
		const float start = ( g_layerBottom - middle ) / lightY;
		const float end = ( g_layerTop - middle ) / lightY;
		const int steps = 24;
		const float stepLength = ( end - start ) / steps;
		float depth = 0.0f;
		[loop] for( int i = 0; i < steps; ++i )
		{
			const float3 world = float3( plane.x, middle, plane.y ) + g_lightDirection * ( start + stepLength * ( i + 0.5f ) );
			depth += cloudDensity( world, world.y, false ) * stepLength;
		}
		shadow = exp( -depth * g_shadowStrength );
	}
	g_cloudShadowOut[id.xy] = shadow;
}
