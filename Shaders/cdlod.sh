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
	float  g_splatDetailTile;		// сторона плитки детального слоя весов, м (terrain.ps, TerrainDetailSplat); 0 — плиток нет
	int    g_waterBedLayer;			// слой гальки под водой (режим static): по глубине в точке (−1 — нет)
	uint   g_showWind;				// поле ветра на рельефе (wind_field.sh) — отладка, «Show wind»
	uint   g_wetnessBaked;			// мокрость от воды static запечена в плитках детального слоя весов (terrain.ps)
	int    g_paintPreview;			// цель кисти поверх земли: −1 — нет, 0…7 — слой, 8 — мокрость, 9 — маска (terrain.ps)
	float  g_displacementStart;		// дисплейсмент (cdlod.vs, terrain_layers.sh): полный ближе, м,
	float  g_displacementEnd;		// к этому расстоянию от точки LOD — нет; 0 — выключен
	float4 g_detailMorph[6];		// уровни мельче листа (−1…−6): x — начало морфинга, y — 1 / длина зоны
	float4 g_layerTint[8];			// множитель альбедо по слоям (xyz; TerrainLayers.albedo_tint, окно «Layer tint»)
	float4 g_layerDisplacement[2];	// размах смещения по слоям, по четыре, м (TerrainLayers.displacement, «Layer displacement»)
};

// Копия карты высот в R32_FLOAT с полной цепочкой мипов, в вершинном и пиксельном шейдерах
DM_SRV( Texture2D, g_heightMap, 0 );

struct PixelInputType
{
	float4 position : SV_POSITION;
	float3 worldPosition : WORLDPOS0;	// со смещением (дисплейсмент): свет, тень, туман
	float3 basePosition : WORLDPOS1;	// без смещения: текстуры, веса и проекции слоёв (текстура не плывёт по смещённой земле)
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

// Нормаль рельефа: центральные разности в мипе карты высот, тексель которого примерно равен пикселю экрана.
// Геометрия вдали грубее карты высот, но освещение по такой нормали остаётся детальным
float3 terrainNormal( float2 worldXZ, float mip )
{
	float offset = g_texelSize * exp2( mip );
	float left  = sampleHeight( worldXZ - float2( offset, 0.0f ), mip );
	float right = sampleHeight( worldXZ + float2( offset, 0.0f ), mip );
	float back  = sampleHeight( worldXZ - float2( 0.0f, offset ), mip );
	float front = sampleHeight( worldXZ + float2( 0.0f, offset ), mip );
	return normalize( float3( left - right, 2.0f * offset, back - front ) );
}

// Нормаль по самой детальной земле (плитка у русла или карта высот) — центральные разности через offset, м
float3 fineNormal( float2 worldXZ, float offset )
{
	float left  = sampleFineHeight( worldXZ - float2( offset, 0.0f ) );
	float right = sampleFineHeight( worldXZ + float2( offset, 0.0f ) );
	float back  = sampleFineHeight( worldXZ - float2( 0.0f, offset ) );
	float front = sampleFineHeight( worldXZ + float2( 0.0f, offset ) );
	return normalize( float3( left - right, 2.0f * offset, back - front ) );
}

#endif
