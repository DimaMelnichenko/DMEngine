////////////////////////////////////////////////////////////////////////////////
// Небо: рассеяние света в атмосфере Земли по модели S. Hillaire, «A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique» (EGSR 2020) — на ней построен Sky Atmosphere в UE5. Рассеяние Рэлея (воздух),
// Ми (аэрозоль), поглощение озоном и многократное рассеяние через таблицу Ψ (Shaders/sky_multiscattering.ps);
// пропускание до края атмосферы — из таблицы (Shaders/sky_transmittance.ps), а не трассировкой на каждом шаге.
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

// Оптическая толщина от точки до края атмосферы трассировкой — по ней строится таблица пропускания
// (Shaders/sky_transmittance.ps). Планету луч не замечает: её тень учитывает выборка таблицы
float3 opticalDepthToTop( float3 position, float3 direction, int samples )
{
	float stepLength = raySphere( position, direction, atmosphereRadius ).y / samples;
	float3 opticalDepth = 0.0f;
	[loop] for( int i = 0; i < samples; ++i )
	{
		float3 samplePosition = position + direction * ( stepLength * ( i + 0.5f ) );
		opticalDepth += atmosphereMedium( max( length( samplePosition ) - planetRadius, 0.0f ) ).extinction * stepLength;
	}
	return opticalDepth;
}

// Координаты таблиц по доле [0, 1]: крайние значения — центры крайних текселей, чтобы края таблицы были точными
float2 lutUvFromUnit( float2 unit, float2 size )
{
	return ( unit * ( size - 1.0f ) + 0.5f ) / size;
}

float2 lutUnitFromUv( float2 uv, float2 size )
{
	return ( uv * size - 0.5f ) / ( size - 1.0f );
}

// Таблица пропускания до края атмосферы, SKY_TRANSMITTANCE_LUT_WIDTH × HEIGHT (Shaders/sky_transmittance.ps)
Texture2D<float4> g_transmittanceLut : register( t2 );
static const float2 transmittanceLutSize = float2( SKY_TRANSMITTANCE_LUT_WIDTH, SKY_TRANSMITTANCE_LUT_HEIGHT );
// Расстояние от земли до края атмосферы по касательной к планете
static const float atmosphereHorizon = sqrt( ( atmosphereRadius - planetRadius ) * ( atmosphereRadius + planetRadius ) );

// Расстояние до горизонта с высоты height — ρ = √(r² − R²), без вычитания квадратов радиусов (точность float в метрах)
float horizonDistance( float height )
{
	return sqrt( max( height * ( 2.0f * planetRadius + height ), 0.0f ) );
}

// Параметризация Bruneton 2017, как у Hillaire: u — доля пути до края атмосферы между кратчайшим (вверх) и самым
// длинным (по касательной к планете), v — высота через расстояние до горизонта
float2 transmittanceLutUv( float height, float cosZenith )
{
	const float radius = planetRadius + height;
	const float rho = horizonDistance( height );
	// r²·μ² + (R_top² − r²): квадрат радиуса края — разностью, без вычитания больших чисел
	const float discriminant = radius * radius * cosZenith * cosZenith + ( atmosphereRadius - radius ) * ( atmosphereRadius + radius );
	const float distance = max( -radius * cosZenith + sqrt( max( discriminant, 0.0f ) ), 0.0f );
	const float minDistance = atmosphereRadius - radius;
	const float maxDistance = rho + atmosphereHorizon;
	return lutUvFromUnit( float2( ( distance - minDistance ) / ( maxDistance - minDistance ), rho / atmosphereHorizon ),
						  transmittanceLutSize );
}

// Обратное: высота и косинус зенитного угла центра текселя таблицы
void transmittanceLutParameters( float2 uv, out float height, out float cosZenith )
{
	const float2 unit = lutUnitFromUv( uv, transmittanceLutSize );
	const float rho = atmosphereHorizon * unit.y;
	const float radius = sqrt( rho * rho + planetRadius * planetRadius );
	height = rho * rho / ( radius + planetRadius );	// r − R = ρ² / (r + R)
	const float minDistance = atmosphereRadius - radius;
	const float maxDistance = rho + atmosphereHorizon;
	const float distance = minDistance + unit.x * ( maxDistance - minDistance );
	cosZenith = distance <= 0.0f ? 1.0f :
		clamp( ( atmosphereHorizon * atmosphereHorizon - rho * rho - distance * distance ) / ( 2.0f * radius * distance ), -1.0f, 1.0f );
}

// Пропускание от точки на высоте height до края атмосферы по направлению с косинусом зенитного угла cosZenith;
// луч в планету (ниже касательной к горизонту) — 0: тень Земли
float3 transmittanceToTop( float height, float cosZenith )
{
	if( cosZenith < -horizonDistance( height ) / ( planetRadius + height ) )
		return 0.0f;
	return g_transmittanceLut.SampleLevel( g_SamplerLinearClamp, transmittanceLutUv( height, cosZenith ), 0.0f ).rgb;
}

float3 transmittanceToTop( float3 position, float3 direction )
{
	const float radius = length( position );
	return transmittanceToTop( radius - planetRadius, dot( position / radius, direction ) );
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
// пропускание отрезка. На единицу освещённости от солнца. Так считаются и небо вокруг наблюдателя (traceAtmosphere,
// Shaders/sky_view.ps), и воздушная перспектива по отрезкам до слоёв объёма (Shaders/aerial_perspective.cs)
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
		float3 sunTransmittance = transmittanceToTop( height, sunCosZenith );
		float3 scattered = ( medium.rayleigh * rayleighPhase + medium.mie * miePhase ) * sunTransmittance +
						   medium.scattering * multipleScattering( height, sunCosZenith );

		// Аналитический интеграл по отрезку при постоянной среде (Hillaire 2015): не теряет энергию на длинных шагах
		float3 segmentTransmittance = exp( -medium.extinction * stepLength );
		luminance += transmittance * ( scattered - scattered * segmentTransmittance ) / max( medium.extinction, 1e-12f );
		transmittance *= segmentTransmittance;
	}
}

// Луч взгляда наблюдателя на высоте observerAltitude до края атмосферы или до земли
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
#endif

#endif
