////////////////////////////////////////////////////////////////////////////////
// Высота рельефа для тех, кто на нём стоит (расстановка, частицы), без привязки к устройству террейна:
// параметры приходят из TerrainHeightSource (Scene\Terrain\TerrainHeightSource.h). Карта высот покрывает
// квадрат [0, worldSize] по X и Z, ось Z идёт по текстуре снизу вверх — как heightMapUV в cdlod.sh
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_HEIGHT_SH
#define TERRAIN_HEIGHT_SH

#include "samplers.sh"

cbuffer TerrainHeightBuffer : register( b5 )
{
	float g_terrainWorldSize;
	float g_terrainHeightMultipler;
	float g_terrainHeightOffset;
	float g_terrainPadding;
};

Texture2D g_terrainHeightMap : register( t0 );

float2 terrainUV( float2 worldXZ )
{
	float2 uv = worldXZ / g_terrainWorldSize;
	uv.y = 1.0f - uv.y;
	return uv;
}

float terrainHeight( float2 worldXZ )
{
	return g_terrainHeightMap.SampleLevel( g_SamplerLinearClamp, terrainUV( worldXZ ), 0.0f ).r * g_terrainHeightMultipler +
		   g_terrainHeightOffset;
}

// Нормаль рельефа по центральным разностям через тексель карты высот
float3 terrainNormal( float2 worldXZ )
{
	uint width, height;
	g_terrainHeightMap.GetDimensions( width, height );
	float offset = g_terrainWorldSize / width;

	float left  = terrainHeight( worldXZ - float2( offset, 0.0f ) );
	float right = terrainHeight( worldXZ + float2( offset, 0.0f ) );
	float back  = terrainHeight( worldXZ - float2( 0.0f, offset ) );
	float front = terrainHeight( worldXZ + float2( 0.0f, offset ) );
	return normalize( float3( left - right, 2.0f * offset, back - front ) );
}

#endif
