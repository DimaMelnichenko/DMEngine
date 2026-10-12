////////////////////////////////////////////////////////////////////////////////
// Поле ветра уровня (класс Wind, константы cb_wind* кадра): порывы волнами по миру — общее для изгиба растений
// (wind.sh) и частиц (particles.cs), без констант материалов
////////////////////////////////////////////////////////////////////////////////

#ifndef WIND_FIELD_SH
#define WIND_FIELD_SH

#include "common.vs"
#include "samplers.sh"
#include "bindless.sh"

static const float windTwoPi = 6.2831853f;

// Поле ветра по рельефу (TerrainWind, CDLODTerrain; SLOT_WIND_FIELD) в координатах карты высот: xz — скорость по X и Z
// мира в долях ветра уровня (направление × множитель: гребни и проходы — больше 1, лес и подветренная тень — меньше), y —
// вертикальная вдоль рельефа (+ вверх по склону), w — порывистость 0…1
DM_SRV( Texture2D<float4>, g_windFieldMap, SLOT_WIND_FIELD );

float4 windFieldSample( float2 uv )
{
	return g_windFieldMap.SampleLevel( g_SamplerLinearClamp, uv, 0.0f );
}

// Цвет множителя скорости для отладочных видов: синий — штиль, голубой — половина, зелёный — как ветер уровня, жёлтый —
// в полтора раза быстрее, красный — вдвое и больше
float3 windSpeedColor( float speed )
{
	const float3 ramp[5] = { float3( 0.05f, 0.15f, 1.0f ), float3( 0.0f, 0.75f, 1.0f ), float3( 0.1f, 0.85f, 0.1f ), float3( 1.0f, 0.85f, 0.0f ),
							 float3( 1.0f, 0.08f, 0.04f ) };
	const float t = saturate( speed * 0.5f ) * 4.0f;
	const uint i = min( uint( t ), 3u );
	return lerp( ramp[i], ramp[i + 1], t - i );
}

// Ветер в точке мира по полю: direction — куда дует (нормированное x, z), speed — множитель скорости ветра уровня,
// vertical — вертикальная доля (+ вверх по склону), turbulence — порывистость 0…1. Без поля (cb_windFieldScale 0) и за
// краем карты — ровный ветер уровня
struct WindSample
{
	float2 direction;
	float  speed;
	float  vertical;
	float  turbulence;
};

WindSample windAt( float2 worldXZ )
{
	WindSample result;
	result.direction = cb_windDirection;
	result.speed = 1.0f;
	result.vertical = 0.0f;
	result.turbulence = 0.0f;
	[branch] if( cb_windFieldScale > 0.0f )
	{
		float2 uv = worldXZ * cb_windFieldScale;
		uv.y = 1.0f - uv.y;
		if( all( uv >= 0.0f ) && all( uv <= 1.0f ) )
		{
			const float4 field = windFieldSample( uv );
			result.speed = length( field.xz );
			result.direction = result.speed > 1e-4f ? field.xz / result.speed : cb_windDirection;
			result.vertical = field.y;
			result.turbulence = field.w;
		}
	}
	return result;
}

// Нагрузка ветра на растение — как давление, растёт с квадратом скорости (не больше втрое против ветра уровня)
float windLoad( WindSample wind )
{
	return min( wind.speed * wind.speed, 3.0f );
}

// Порывистость (за гребнем, на опушке): направление качается вокруг поля до ±45° × порывистость, своё колебание у точки
float2 windSwirl( WindSample wind, float phase )
{
	const float angle = wind.turbulence * 0.8f * sin( windTwoPi * ( cb_gameTime * ( 0.6f + 0.5f * phase ) + phase ) );
	float s, c;
	sincos( angle, s, c );
	return float2( wind.direction.x * c - wind.direction.y * s, wind.direction.x * s + wind.direction.y * c );
}

// Порыв в точке мира: доля силы между cb_windGustMin (между волнами) и cb_windGustMax (на гребне). Две волны вдоль
// ветра с разной длиной и фронт, изогнутый поперёк ветра, — не ровные полосы; бегут со скоростью cb_windSpeed
float windGust( float2 worldXZ )
{
	const float2 across = float2( -cb_windDirection.y, cb_windDirection.x );
	const float along = ( dot( worldXZ, cb_windDirection ) - cb_gameTime * cb_windSpeed ) / cb_windGustSize;
	const float side = dot( worldXZ, across ) / cb_windGustSize;
	const float wave = 0.6f * sin( windTwoPi * along + 1.7f * sin( 1.3f * side ) ) +
					   0.4f * sin( windTwoPi * 0.43f * along + 2.1f * side + 1.0f );
	return lerp( cb_windGustMin, cb_windGustMax, wave * 0.5f + 0.5f );
}

#endif
