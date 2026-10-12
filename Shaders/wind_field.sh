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
