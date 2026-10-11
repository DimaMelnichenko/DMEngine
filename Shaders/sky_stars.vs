////////////////////////////////////////////////////////////////////////////////
// Звёзды каталога (таблица Stars, Yale Bright Star Catalogue; SkyAtmosphere, docs/sky.md, «Ночь») — квад в несколько
// пикселей на звезду по SV_VertexID (6 вершин на звезду, без вершинного буфера) на дальней плоскости, после фона неба,
// со сложением. Направление — экваториальное (J2000) в мире через базис звёздного времени (SunPosition::equatorialFrame).
// Освещённость звезды — пятном гаусса в ~пиксель (Shaders/sky_stars.ps): яркость пятна, проинтегрированная по экрану, —
// её освещённость, как бы ни менялись разрешение и угол зрения. Свет ослаблен воздухом (пропускание по лучу взгляда) и
// туманом уровня до дальней плоскости. Мерцание (сцинтилляция) — от воздушной массы: в зените почти нет, у горизонта
// сильное и разноцветное
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "samplers.sh"
#define ATMOSPHERE_NO_SKY
#include "atmosphere.sh"
#include "exposure.sh"
#include "height_fog.sh"
#include "night_sky.sh"
#include "bindless.sh"

// Раскладка — SkyAtmosphere::StarData
struct Star
{
	float3 direction;		// экваториальные координаты J2000: x — к точке весеннего равноденствия, z — к полюсу мира
	float  illuminance;		// освещённость от звезды над атмосферой, лк
	float3 color;			// линейный цвет с яркостью 1
	float  padding;
};

DM_SRV( StructuredBuffer<Star>, g_stars, 6 );

// Полуширина квада, пиксели: 3,3 σ пятна (Shaders/sky_stars.ps) — за ним меньше тысячной освещённости
static const float starQuadRadius = 2.0f;
// Ниже горизонта звезду закрывает земля (пропускание 0); у самого горизонта — запас под наклон рельефа вдали
static const float starMinHeight = -0.05f;

// Мерцание: σ относительной яркости — σ₀·X^1,75 (Young 1967, для зрачка ~7 мм), не больше предела; X — воздушная масса.
// Узор — гладкий шум двух частот, Гц; у горизонта (X от chromaticStart до chromaticFull) каналы мерцают порознь — цвет
// звезды вспыхивает: лучи разных длин волн из-за дисперсии в воздухе идут через разные вихри
static const float twinkleZenith = 0.1f;
static const float twinkleMax = 0.8f;
static const float twinkleFrequency0 = 6.0f;
static const float twinkleFrequency1 = 11.0f;
static const float chromaticStart = 3.0f;
static const float chromaticFull = 10.0f;

struct StarVertex
{
	float4 position : SV_POSITION;
	float2 offset : TEXCOORD0;				// от центра звезды, пиксели
	nointerpolation float3 color : COLOR0;	// яркость пятна единичной площади (в пикселях) с экспозицией
};

uint hash( uint x )
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

float random( uint seed, uint index )
{
	return ( hash( seed ^ hash( index + 0x9e3779b9u ) ) & 0x00ffffffu ) / 16777216.0f;
}

// Гладкий шум во времени −1…1: случайные значения в целых точках, между ними — сглаженно
float timeNoise( uint seed, float t )
{
	const float step = floor( t );
	const float f = t - step;
	const uint index = (uint)(int)step;
	return lerp( random( seed, index ), random( seed, index + 1u ), f * f * ( 3.0f - 2.0f * f ) ) * 2.0f - 1.0f;
}

float twinkleNoise( uint seed )
{
	return 0.7f * ( timeNoise( seed, cb_gameTime * twinkleFrequency0 ) + timeNoise( hash( seed ), cb_gameTime * twinkleFrequency1 ) );
}

// Воздушная масса по высоте над горизонтом (Kasten, Young 1989): 1 в зените, ~38 у горизонта
float airMass( float sinHeight )
{
	const float height = degrees( asin( saturate( sinHeight ) ) );
	return 1.0f / ( saturate( sinHeight ) + 0.50572f * pow( height + 6.07995f, -1.6364f ) );
}

// Множитель яркости по каналам от мерцания
float3 twinkle( uint star, float sinHeight )
{
	const float mass = airMass( sinHeight );
	const float sigma = min( twinkleZenith * pow( mass, 1.75f ), twinkleMax ) * g_starTwinkle;
	const uint seed = hash( star * 0x01000193u );
	const float common = twinkleNoise( seed );
	const float3 channels = float3( twinkleNoise( seed ^ 0x68e31da4u ), twinkleNoise( seed ^ 0xb5297a4du ), twinkleNoise( seed ^ 0x1b56c4e9u ) );
	const float chromatic = saturate( ( mass - chromaticStart ) / ( chromaticFull - chromaticStart ) );
	return max( 1.0f + sigma * lerp( common.xxx, channels, chromatic ), 0.0f );
}

StarVertex main( uint vertexId : SV_VertexID )
{
	StarVertex output;
	// Пустой треугольник за экраном — звезда не рисуется
	output.position = float4( -2.0f, -2.0f, 0.0f, 1.0f );
	output.offset = 0.0f;
	output.color = 0.0f;

	const uint index = vertexId / 6u;
	const Star star = g_stars[index];
	const float3 direction = star.direction.x * g_equatorialX + star.direction.y * g_equatorialY + star.direction.z * g_equatorialZ;
	if( direction.y < starMinHeight )
		return output;
	// Точка на бесконечности: только поворот вида и проекция
	const float4 clip = mul( float4( direction, 0.0f ), cb_viewProjectionMatrix );
	if( clip.w <= 0.0f )
		return output;
	const float2 ndc = clip.xy / clip.w;

	// Углы квада: два треугольника (−1, −1), (1, −1), (−1, 1) и (−1, 1), (1, −1), (1, 1)
	const uint corner = vertexId % 6u;
	const float2 side = float2( corner == 1u || corner == 4u || corner == 5u ? 1.0f : -1.0f,
								corner == 2u || corner == 3u || corner == 5u ? 1.0f : -1.0f );
	output.offset = side * starQuadRadius;
	// Глубина 0 — дальняя плоскость (Reversed-Z): звезда видна только там, где сцена ничего не нарисовала
	output.position = float4( ndc + output.offset * 2.0f / g_viewportSize, 0.0f, 1.0f );

	// Телесный угол пикселя в направлении звезды: шаг экрана в тангенсах угла × cos³ от оси взгляда
	const float2 tangentStep = 2.0f / ( g_viewportSize * float2( cb_projectionMatrix[0][0], cb_projectionMatrix[1][1] ) );
	const float2 tangent = ndc / float2( cb_projectionMatrix[0][0], cb_projectionMatrix[1][1] );
	const float pixelSolidAngle = tangentStep.x * tangentStep.y * pow( 1.0f + dot( tangent, tangent ), -1.5f );

	// Над атмосферой → у наблюдателя: воздух по лучу взгляда, затем туман уровня до фона (без его свечения — оно в фоне)
	const float3 air = transmittanceToTop( observerAltitude, direction.y );
	const float3 fog = applyHeightFogToSky( 1.0f, direction ) - applyHeightFogToSky( 0.0f, direction );
	const float illuminance = star.illuminance * g_starIlluminanceScale * cb_skyScale;
	output.color = star.color * illuminance / pixelSolidAngle * air * fog * twinkle( index, direction.y ) * preExposure();
	return output;
}
