////////////////////////////////////////////////////////////////////////////////
// Константы шейдеров террейна CDLOD (Shaders/cdlod.sh — cdlod.vs, terrain.ps, cdlod_lod.ps).
// Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_CDLOD_CONSTANTS_H
#define DM_CDLOD_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"
#include "terrain_layer_constants.h"

// Уровней LOD квадродерева и уровней мельче листа (CDLODTerrain::maxLevels, detailLevels)
#define CDLOD_MAX_LEVELS 16
#define CDLOD_DETAIL_LEVELS 6

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( CDLODBuffer, SLOT_CB_MATERIAL )
{
	DM_FIELD( float, worldSize );				// сторона террейна в мировых единицах
	DM_FIELD( float, heightMultiplier );
	DM_FIELD( float, heightOffset );
	DM_FIELD( float, gridDim );				// квадов в стороне патча
	DM_ARRAY( float4, morphConsts, CDLOD_MAX_LEVELS );		// по уровням LOD: x — начало морфинга, y — 1 / длина зоны морфинга
	DM_FIELD( float, texelSize );				// сторона текселя карты высот в мировых единицах
	DM_FIELD( float, farTextureScale );		// во сколько раз крупнее повтор текстур вдали (distance resampling)
	DM_FIELD( float, farBlendStart );			// переход к крупному масштабу: от этого расстояния до камеры, м,
	DM_FIELD( float, farBlendEnd );			// до этого — дальше только крупный
	DM_FIELD( uint, showWater );				// подсветка воды симуляции (water.sh) — отладка, «Show water»
	DM_FIELD( float, detailTile );			// сторона детальной плитки, м (terrain_detail.sh); 0 — детальной земли нет
	DM_FIELD( int, waterBedLayer );			// слой гальки под водой (режим static): по глубине в точке (−1 — нет)
	DM_FIELD( uint, showWind );				// поле ветра на рельефе (wind_field.sh) — отладка, «Show wind»
	DM_FIELD( uint, wetnessBaked );			// мокрость от воды static запечена в плитках детального слоя весов (terrain.ps)
	DM_FIELD( int, paintPreview );			// цель кисти поверх земли: −1 — нет, 0…maxLayers − 1 — слой, maxLayers — мокрость, maxLayers + 1 — маска (terrain.ps)
	DM_FIELD( float, microShadows );			// микротени прямого света по AO слоёв, 0…1 (terrain.ps, «Micro shadows»)
	// Наносы на дне нижних озёр (terrain.ps, lakeBedShares; правила «Water beds»): слой дресвы (−1 — нет), глубина воды
	// перехода гальки к дресве (x → y) и дресвы к илу (z → w), м; уровень озера, м мира: от x к y наносы сходят на нет;
	// слой ила (−1 — нет)
	DM_FIELD( int, lakeGravelLayer );
	DM_FIELD( float4, lakeBedDepths );
	DM_FIELD( float2, lakeSedimentLevel );
	DM_FIELD( int, lakeSiltLayer );
	DM_FIELD( float, cdlodPadding );
	DM_FIELD( float3, paintPreviewColor );		// цвет подсветки paintPreview: цель кисти — оранжевый, места звука — бирюзовый
	DM_FIELD( float, paintPreviewStrength );	// её сила, 0…1: у кисти 0,75, у мест звука 0,5 (земля под ней видна)
	DM_ARRAY( float4, detailMorph, CDLOD_DETAIL_LEVELS );		// уровни мельче листа (−1…−6): x — начало морфинга, y — 1 / длина зоны
	DM_ARRAY( float4, layerOcclusion, TERRAIN_SPLAT_SLICES );		// сила AO по слоям, по четыре, 0…1 (TerrainLayers.occlusion, «Layer occlusion»)
	DM_FIELD_STRUCT( TerrainLayerConstants, terrainLayers );	// то же, что ресурс сцены SLOT_TERRAIN_LAYER_CONSTANTS (terrain_layers.sh)
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
