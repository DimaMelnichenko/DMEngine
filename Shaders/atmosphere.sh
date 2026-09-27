////////////////////////////////////////////////////////////////////////////////
// Небо: рассеяние света в атмосфере Земли по модели S. Hillaire, «A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique» (EGSR 2020) — на ней построен Sky Atmosphere в UE5. Рассеяние Рэлея (воздух),
// Ми (аэрозоль), поглощение озоном и многократное рассеяние через таблицу Ψ (Shaders/sky_multiscattering.ps).
// Параметры атмосферы — значения по умолчанию UE (atmosphere_constants.h, общие с C++). Класс SkyAtmosphere
////////////////////////////////////////////////////////////////////////////////

#ifndef ATMOSPHERE_SH
#define ATMOSPHERE_SH

#include "slots.h"
#include "atmosphere_constants.h"
#include "cubemap.sh"

#include "samplers.sh"

// Раскладка — SkyAtmosphere::Parameters
cbuffer SkyParameters : register( SLOT_CB_PASS )
{
	float3 g_sunDirection;	// направление на солнце
	float  g_skyIntensity;	// множитель рассеянного света (1 — по модели)
	float3 g_sunColor;		// цветность солнца над атмосферой (яркость 1 — небо запекается для солнца 1 лк)
	float  g_haze;			// множитель плотности аэрозоля (Ми): больше — дымка у горизонта и ореол вокруг солнца
	float3 g_groundAlbedo;
	int    g_face;			// грань cubemap неба: 0…5 — +X, −X, +Y, −Y, +Z, −Z (cubeDirection)
};

static const float atmospherePi = 3.14159265f;
static const float planetRadius = ATMOSPHERE_PLANET_RADIUS;
static const float atmosphereRadius = ATMOSPHERE_TOP_RADIUS;
static const float observerAltitude = ATMOSPHERE_OBSERVER_ALTITUDE;
static const float3 rayleighScattering = float3( ATMOSPHERE_RAYLEIGH_SCATTERING );
static const float rayleighScaleHeight = ATMOSPHERE_RAYLEIGH_SCALE_HEIGHT;
static const float mieScattering = ATMOSPHERE_MIE_SCATTERING;
static const float mieAbsorption = ATMOSPHERE_MIE_ABSORPTION;
static const float mieScaleHeight = ATMOSPHERE_MIE_SCALE_HEIGHT;
static const float mieAnisotropy = ATMOSPHERE_MIE_ANISOTROPY;
static const float3 ozoneAbsorption = float3( ATMOSPHERE_OZONE_ABSORPTION );	// слой 10…40 км, пик на 25 км

// Расстояния до входа и выхода луча из сферы; y < 0 — промах или сфера позади
float2 raySphere( float3 origin, float3 direction, float radius )
{
	float b = dot( origin, direction );
	float c = dot( origin, origin ) - radius * radius;
	float d = b * b - c;
	if( d < 0.0f )
		return float2( -1.0f, -1.0f );
	float s = sqrt( d );
	return float2( -b - s, -b + s );
}

struct Medium
{
	float3 rayleigh;	// рассеяние Рэлея
	float3 mie;			// рассеяние Ми
	float3 scattering;
	float3 extinction;
};

Medium atmosphereMedium( float height )
{
	float rayleighDensity = exp( -height / rayleighScaleHeight );
	float mieDensity = exp( -height / mieScaleHeight ) * g_haze;
	float ozoneDensity = max( 0.0f, 1.0f - abs( height - ATMOSPHERE_OZONE_CENTER ) / ATMOSPHERE_OZONE_HALF_WIDTH );

	Medium medium;
	medium.rayleigh = rayleighScattering * rayleighDensity;
	medium.mie = mieScattering * mieDensity;
	medium.scattering = medium.rayleigh + medium.mie;
	medium.extinction = medium.scattering + mieAbsorption * mieDensity + ozoneAbsorption * ozoneDensity;
	return medium;
}

// Пропускание от точки до края атмосферы; 0 — луч упирается в планету
float3 transmittanceToTop( float3 position, float3 direction, int samples )
{
	if( raySphere( position, direction, planetRadius ).x > 0.0f )
		return 0.0f;

	float stepLength = raySphere( position, direction, atmosphereRadius ).y / samples;
	float3 opticalDepth = 0.0f;
	[loop] for( int i = 0; i < samples; ++i )
	{
		float3 samplePosition = position + direction * ( stepLength * ( i + 0.5f ) );
		opticalDepth += atmosphereMedium( max( length( samplePosition ) - planetRadius, 0.0f ) ).extinction * stepLength;
	}
	return exp( -opticalDepth );
}

float phaseRayleigh( float cosTheta )
{
	return 3.0f / ( 16.0f * atmospherePi ) * ( 1.0f + cosTheta * cosTheta );
}

// Корнетт — Шенкс: вариант Хеньи — Гринстейна с физичным рассеянием назад
float phaseMie( float cosTheta )
{
	const float g = mieAnisotropy;
	return 3.0f / ( 8.0f * atmospherePi ) * ( ( 1.0f - g * g ) * ( 1.0f + cosTheta * cosTheta ) ) /
		   ( ( 2.0f + g * g ) * pow( abs( 1.0f + g * g - 2.0f * g * cosTheta ), 1.5f ) );
}

#ifndef ATMOSPHERE_NO_SKY
// Таблица многократного рассеяния Ψ (32 × 32): u — косинус зенитного угла солнца, v — высота над землёй
Texture2D<float4> g_multipleScattering : register(t1);

float3 multipleScattering( float height, float sunCosZenith )
{
	float2 uv = float2( sunCosZenith * 0.5f + 0.5f, saturate( height / ( atmosphereRadius - planetRadius ) ) );
	return g_multipleScattering.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).rgb;
}

struct AtmosphereSample
{
	float3 luminance;		// рассеянный к наблюдателю свет на единицу освещённости от солнца
	float3 transmittance;	// пропускание вдоль луча взгляда
	bool hitsGround;
};

// Отрезок луча взгляда от start до end метров из origin (samples шагов): рассеянный к наблюдателю свет
// прибавляется к luminance — с пропусканием от наблюдателя до начала отрезка, transmittance умножается на
// пропускание отрезка. На единицу освещённости от солнца. Так считаются и небо (traceAtmosphere), и воздушная
// перспектива по отрезкам до слоёв объёма (Shaders/aerial_perspective.cs)
void integrateScattering( float3 origin, float3 direction, float start, float end, int samples,
						  inout float3 luminance, inout float3 transmittance )
{
	const float cosTheta = dot( direction, g_sunDirection );
	const float rayleighPhase = phaseRayleigh( cosTheta );
	const float miePhase = phaseMie( cosTheta );

	const float stepLength = ( end - start ) / samples;
	[loop] for( int i = 0; i < samples; ++i )
	{
		float3 position = origin + direction * ( start + stepLength * ( i + 0.5f ) );
		float radius = length( position );
		float height = radius - planetRadius;
		float sunCosZenith = dot( position / radius, g_sunDirection );
		Medium medium = atmosphereMedium( height );

		// Однократное рассеяние солнца и многократное (изотропное, из таблицы)
		float3 sunTransmittance = transmittanceToTop( position, g_sunDirection, 8 );
		float3 scattered = ( medium.rayleigh * rayleighPhase + medium.mie * miePhase ) * sunTransmittance +
						   medium.scattering * multipleScattering( height, sunCosZenith );

		// Аналитический интеграл по отрезку при постоянной среде (Hillaire 2015): не теряет энергию на длинных шагах
		float3 segmentTransmittance = exp( -medium.extinction * stepLength );
		luminance += transmittance * ( scattered - scattered * segmentTransmittance ) / max( medium.extinction, 1e-12f );
		transmittance *= segmentTransmittance;
	}
}

AtmosphereSample traceAtmosphere( float3 direction, int samples )
{
	AtmosphereSample result = (AtmosphereSample)0;
	const float3 origin = float3( 0.0f, planetRadius + observerAltitude, 0.0f );

	float pathLength = raySphere( origin, direction, atmosphereRadius ).y;
	float2 ground = raySphere( origin, direction, planetRadius );
	result.hitsGround = ground.x > 0.0f;
	if( result.hitsGround )
		pathLength = ground.x;

	float3 transmittance = 1.0f;
	float3 luminance = 0.0f;
	integrateScattering( origin, direction, 0.0f, pathLength, samples, luminance, transmittance );

	result.luminance = luminance;
	result.transmittance = transmittance;
	return result;
}

// Яркость земли под горизонтом: ламбертова поверхность под солнцем (через атмосферу) и небом. Свет неба — среднее
// яркости по пяти направлениям (зенит и четыре на высоте 30°) вместо интеграла по полусфере
float3 groundRadiance( float3 direction, float distance )
{
	float3 position = float3( 0.0f, planetRadius + observerAltitude, 0.0f ) + direction * distance;
	float3 up = normalize( position );
	float3 sun = transmittanceToTop( position, g_sunDirection, 8 ) * saturate( dot( up, g_sunDirection ) ) / atmospherePi;

	float3 sky = traceAtmosphere( float3( 0.0f, 1.0f, 0.0f ), 12 ).luminance;
	[loop] for( int k = 0; k < 4; ++k )
	{
		float azimuth = k * 0.5f * atmospherePi;
		sky += traceAtmosphere( float3( 0.866f * cos( azimuth ), 0.5f, 0.866f * sin( azimuth ) ), 12 ).luminance;
	}
	return g_groundAlbedo * ( sun + sky / 5.0f * g_skyIntensity );
}

// Яркость неба в направлении direction без солнечного диска (диск рисует только фон: прямой свет солнца уже даёт
// направленный источник, в освещении окружением он был бы учтён дважды)
float3 skyRadiance( float3 direction )
{
	AtmosphereSample sample = traceAtmosphere( direction, 32 );
	float3 color = sample.luminance * g_skyIntensity;
	if( sample.hitsGround )
	{
		const float3 origin = float3( 0.0f, planetRadius + observerAltitude, 0.0f );
		color += sample.transmittance * groundRadiance( direction, raySphere( origin, direction, planetRadius ).x );
	}
	return color * g_sunColor;
}
#endif

#endif
