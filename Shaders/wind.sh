////////////////////////////////////////////////////////////////////////////////
// Ветер уровня для растительности — как Wind Directional Source и SimpleGrassWind в UE. Поле ветра одно на уровень
// (класс Wind, константы cb_wind* кадра в Shaders/common.vs): направление, сила и порывы — волны, бегущие по миру со
// скоростью ветра (волны по лугу). Растение гнётся от корня: сдвиг верхушки растёт как квадрат высоты над корнем,
// как у консоли под нагрузкой; у каждого растения ещё своя дрожь. Отклик — параметр материала WindWeight.
// Сдвиг считается в vertexWorldPosition (Shaders/LightShader.vs) — у варианта «только глубина» тот же: depth prepass
// (EQUAL), тени и цвет совпадают
////////////////////////////////////////////////////////////////////////////////

#ifndef WIND_SH
#define WIND_SH

#include "common.vs"

static const float windTwoPi = 6.2831853f;

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

// Сдвиг вершины на высоте height, м, над корнем root растения с откликом weight. Верх не удлиняется: вместе со сдвигом
// вбок вершина опускается (дуга). Дрожь ±25 % с частотой 1,5–2,5 Гц, фаза — своя у каждого растения (хеш корня)
float3 windOffset( float3 root, float height, float weight )
{
	const float phase = frac( sin( dot( root.xz, float2( 12.9898f, 78.233f ) ) ) * 43758.5453f );
	const float flutter = 0.25f * sin( windTwoPi * ( cb_gameTime * ( 1.5f + phase ) + phase ) );
	const float bend = cb_windStrength * weight * windGust( root.xz ) * ( 1.0f + flutter );	// 1/м
	const float2 side = cb_windDirection * ( bend * height * height );
	const float drop = 0.5f * dot( side, side ) / max( height, 0.01f );
	return float3( side.x, -drop, side.y );
}

#endif
