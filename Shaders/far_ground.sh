////////////////////////////////////////////////////////////////////////////////
// Земля дальних гор для тех, кто ставит что-то на неё (лес гор — слой расстановки far_forest, scatter.cs): высота поверхности,
// как у вершин гор у камеры (карта гор билинейно, у края карты уровня — переход к её высоте, far_terrain.sh), и плотность
// леса по правилам гор (FarTerrain, far_forest_density.cs). Ресурсы сцены и константы кадра — FarTerrain
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_FAR_GROUND_SH
#define DM_FAR_GROUND_SH

#include "slots.h"
#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float>, g_farGroundHeight, SLOT_FAR_TERRAIN_HEIGHT );
DM_SRV( Texture2D<float>, g_farForestDensity, SLOT_FAR_FOREST );

// Горы есть (их ресурсы можно читать)
bool farGroundExists()
{
	return cb_farTerrain.z > 0.0f;
}

float2 farGroundUV( float2 worldXZ )
{
	float2 uv = ( worldXZ - cb_farTerrain.xy ) / cb_farTerrain.z;
	uv.y = 1.0f - uv.y;
	return uv;
}

// Расстояние до квадрата карты уровня [0, сторона]², м; внутри — 0
float distanceToLevelMap( float2 worldXZ )
{
	return length( max( max( -worldXZ, worldXZ - cb_terrain.x ), 0.0f ) );
}

// Высота земли гор в точке: mapHeight — высота карты уровня там же (край продолжен наружу) — у края карты, горы — дальше
float farGroundHeight( float2 worldXZ, float mapHeight )
{
	const float far = g_farGroundHeight.SampleLevel( g_SamplerLinearClamp, farGroundUV( worldXZ ), 0.0f );
	return lerp( mapHeight, far, smoothstep( 0.0f, max( cb_farTerrain.w, 1.0f ), distanceToLevelMap( worldXZ ) ) );
}

// Плотность леса гор 0…1 по их правилам (граница леса, уклон, снег, скала, лёд, пятна); mapForest — маска леса карты уровня
// там же (край продолжен наружу): у края карты — она, к «Material blend» — правила гор
float farForestDensity( float2 worldXZ, float mapForest )
{
	const float rules = g_farForestDensity.SampleLevel( g_SamplerLinearClamp, farGroundUV( worldXZ ), 0.0f );
	return lerp( mapForest, rules, smoothstep( 0.0f, max( cb_farForest.x, 1.0f ), distanceToLevelMap( worldXZ ) ) );
}

#endif
