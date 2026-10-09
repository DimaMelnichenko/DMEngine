////////////////////////////////////////////////////////////////////////////////
// Общее для шейдеров террейна CDLOD (cdlod.vs, terrain.ps, cdlod_lod.ps): параметры террейна
// (CDLODTerrain::Parameters), карта высот и выход вершинного шейдера
////////////////////////////////////////////////////////////////////////////////

#ifndef CDLOD_SH
#define CDLOD_SH

#include "slots.h"

#include "samplers.sh"
#include "bindless.sh"
#include "terrain_detail.sh"

cbuffer CDLODBuffer : register( SLOT_CB_MATERIAL )
{
	float  g_worldSize;				// сторона террейна в мировых единицах
	float  g_heightMultiplier;
	float  g_heightOffset;
	float  g_gridDim;				// квадов в стороне патча
	float4 g_morphConsts[16];		// по уровням LOD: x — начало морфинга, y — 1 / длина зоны морфинга
	float4 g_layerScale[2];			// по слоям материала, по четыре: повторов текстуры на единицу мира
	float  g_texelSize;				// сторона текселя карты высот в мировых единицах
	float  g_triplanarSharpness;	// чем больше, тем уже переход между проекциями triplanar
	float  g_heightBlendDepth;		// ширина перехода между слоями при смешивании по высоте
	float  g_farTextureScale;		// во сколько раз крупнее повтор текстур вдали (distance resampling)
	float  g_farBlendStart;			// переход к крупному масштабу: от этого расстояния до камеры, м,
	float  g_farBlendEnd;			// до этого — дальше только крупный
	uint   g_layerCount;			// слоёв в массивах текстур (TerrainLayers)
	uint   g_showWater;				// подсветка воды симуляции (water.sh) — отладка, «Show water»
	float  g_detailTile;			// сторона детальной плитки, м (terrain_detail.sh); 0 — детальной земли нет
	int    g_channelLayer;			// слой гальки русел: в детальных плитках красится по врезу (−1 — нет)
	float2 g_cdlodPadding;
	float4 g_detailMorph[2];		// уровни мельче листа (−1, −2): x — начало морфинга, y — 1 / длина зоны
};

// Копия карты высот в R32_FLOAT с полной цепочкой мипов, в вершинном и пиксельном шейдерах
DM_SRV( Texture2D, g_heightMap, 0 );

struct PixelInputType
{
	float4 position : SV_POSITION;
	float3 worldPosition : WORLDPOS0;
	float2 uv : TEXCOORD0;			// координаты карты высот и splat-карты
	float2 lodDebug : TEXCOORD1;	// x — уровень LOD, y — коэффициент морфинга
};

// Ось z мира идёт по текстуре снизу вверх
float2 heightMapUV( float2 worldXZ )
{
	float2 uv = worldXZ / g_worldSize;
	uv.y = 1.0f - uv.y;
	return uv;
}

float sampleHeight( float2 worldXZ, float mip )
{
	return g_heightMap.SampleLevel( g_SamplerLinearClamp, heightMapUV( worldXZ ), mip ).r * g_heightMultiplier + g_heightOffset;
}

// Высота самой детальной земли в точке: плитка у русла (terrain_detail.sh) или карта высот (мип 0)
float sampleFineHeight( float2 worldXZ )
{
	float detail;
	if( sampleTerrainDetail( worldXZ, g_detailTile, detail ) )
		return detail * g_heightMultiplier + g_heightOffset;
	return sampleHeight( worldXZ, 0.0f );
}

#endif
