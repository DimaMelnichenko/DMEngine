////////////////////////////////////////////////////////////////////////////////
// Высота рельефа для тех, кто на нём стоит (расстановка, частицы), без привязки к устройству террейна:
// параметры приходят из TerrainHeightSource (Scene\Terrain\TerrainHeightSource.h). Карта высот покрывает
// квадрат [0, worldSize] по X и Z, ось Z идёт по текстуре снизу вверх — как heightMapUV в cdlod.sh. Слоты SRV 0 и 1 —
// карта высот и маска очистки растительности правками рельефа (TerrainHeight::foliageClearMask)
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_HEIGHT_SH
#define TERRAIN_HEIGHT_SH

#include "samplers.sh"
#include "bindless.sh"
#include "terrain_detail.sh"

cbuffer TerrainHeightBuffer : register( b5 )
{
	float g_terrainWorldSize;
	float g_terrainHeightMultiplier;
	float g_terrainHeightOffset;
	float g_terrainDetailTile;	// сторона детальной плитки, м (terrain_detail.sh); 0 — детальной земли нет
};

DM_SRV( Texture2D, g_terrainHeightMap, 0 );
DM_SRV( Texture2D, g_terrainFoliageClear, 1 );

float2 terrainUV( float2 worldXZ )
{
	float2 uv = worldXZ / g_terrainWorldSize;
	uv.y = 1.0f - uv.y;
	return uv;
}

// Высота земли в точке: детальная плитка у русла (U-ложе) или карта высот — как самый детальный уровень террейна
float terrainHeight( float2 worldXZ )
{
	float detail;
	const float normalized = sampleTerrainDetail( worldXZ, g_terrainDetailTile, detail ) ? detail :
							 g_terrainHeightMap.SampleLevel( g_SamplerLinearClamp, terrainUV( worldXZ ), 0.0f ).r;
	return normalized * g_terrainHeightMultiplier + g_terrainHeightOffset;
}

// Растительность в детальной плитке у русла: −1 — плитки нет (решают маски 1 м), иначе доля, которую убирает врез русла
// (0,25 м: узкий ручей не выкашивает метровую полосу)
float terrainChannelClear( float2 worldXZ )
{
	float2 detail;
	if( !sampleTerrainDetail2( worldXZ, g_terrainDetailTile, detail ) )
		return -1.0f;
	return smoothstep( 0.02f, 0.08f, detail.y );
}

// Доля растительности, которую убрали правки рельефа (русло, площадка): 0 — не тронута, 1 — убрана вся
float terrainFoliageClear( float2 uv )
{
	return g_terrainFoliageClear.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).r;
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
