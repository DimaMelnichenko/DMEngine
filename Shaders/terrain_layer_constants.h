////////////////////////////////////////////////////////////////////////////////
// Константы слоёв материала террейна (terrain_layers.sh, CDLODTerrain::layerConstants): одна раскладка у структурного
// буфера сцены и у cbuffer террейна. Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_TERRAIN_LAYER_CONSTANTS_H
#define DM_TERRAIN_LAYER_CONSTANTS_H

#include "shader_constants.h"

// Слоёв материала террейна и срезов splat-карты по четыре слоя (TerrainMaterial::maxLayers, splatSlices); кратно четырём
#define TERRAIN_MAX_LAYERS 12
#define TERRAIN_SPLAT_SLICES ( TERRAIN_MAX_LAYERS / 4 )

// То, что нужно выбору слоёв и дисплейсменту: ресурс сцены SLOT_TERRAIN_LAYER_CONSTANTS (структурный буфер из одного
// элемента — расстановка, частицы) и копия в cbuffer террейна (cdlod_constants.h)
DM_STRUCT( TerrainLayerConstants )
{
	DM_MEMBER_ARRAY( float4, layerScale, TERRAIN_SPLAT_SLICES );		// по слоям, по четыре: повторов текстуры на единицу мира
	// размах смещения по слоям, по четыре, м (TerrainLayers.displacement, «Layer displacement»)
	DM_MEMBER_ARRAY( float4, layerDisplacement, TERRAIN_SPLAT_SLICES );
	DM_MEMBER( float3, lodOrigin );				// точка LOD главного вида (у видов теней — та же)
	DM_MEMBER( float, lodEyeHeight );			// ноги ниже точки LOD, м (ходьба; в полёте 0): LOD — от отрезка «глаза — ноги»
	DM_MEMBER( float, worldSize );				// сторона террейна, м
	DM_MEMBER( float, triplanarSharpness );		// чем больше, тем уже переход между проекциями triplanar
	DM_MEMBER( float, heightBlendDepth );		// ширина перехода между слоями при смешивании по высоте
	DM_MEMBER( uint, layerCount );				// слоёв в массивах текстур (TerrainLayers)
	DM_MEMBER( float, splatDetailTile );		// сторона плитки детального слоя весов, м; 0 — плиток нет
	DM_MEMBER( float, displacementStart );		// дисплейсмент: полный ближе, м,
	DM_MEMBER( float, displacementEnd );		// к этому расстоянию от точки LOD — нет; 0 — выключен
	DM_MEMBER( float, forestFloorStrength );	// подстилка под кронами: доля полога, которая уходит в её слой («Forest floor»)
	// множитель альбедо по слоям (xyz; TerrainLayers.albedo_tint, окно «Layer tint»)
	DM_MEMBER_ARRAY( float4, layerTint, TERRAIN_MAX_LAYERS );
	DM_MEMBER( int, forestFloorLayer );			// слой роли forest_floor; −1 — нет
	DM_MEMBER( uint, forestFloorReplaced );		// биты слоёв, чей вес под кронами уходит подстилке: травы
	DM_MEMBER( float, forestFloorEdge );		// рваный край подстилки: сдвиг выборки полога шумом, м («Forest floor edge»)
	DM_MEMBER( int, forestLitterLayer );		// слой роли forest_floor_broadleaf (листья под лиственными); −1 — нет
	DM_MEMBER( int, forestMossLayer );			// слой роли forest_moss; −1 — нет
	DM_MEMBER( uint, forestFloorWet );			// биты слоёв, чей вес — «влажно» (влажная трава): там мха больше
	DM_MEMBER( float, forestMoss );				// доля подстилки, которая уходит мху на влажном («Forest moss»)
	DM_MEMBER( float, forestMossDry );			// на сухом — эта доля от неё («Forest moss on dry»)
	// Высота земли (terrain_blend.sh): карта нормирована — высота × множитель + сдвиг, м; детальные плитки у русел, м (0 — нет)
	DM_MEMBER( float, heightMultiplier );
	DM_MEMBER( float, heightOffset );
	DM_MEMBER( float, detailTile );
	DM_MEMBER( float, microShadows );			// микротени прямого света по AO слоёв, 0…1 («Micro shadows»)
	// Второй масштаб текстур вдали (terrain_material.sh): во сколько раз крупнее повтор; переход — от start до end, м
	DM_MEMBER( float, farTextureScale );
	DM_MEMBER( float, farBlendStart );
	DM_MEMBER( float, farBlendEnd );
	// Берег озёр (terrain_layers.sh, lakeShoreBand): запас берега — вода ниже перелива на столько, м (lake_freeboard; 0 —
	// полосы нет); слои расстановки «Shallows» — до этой глубины воды, м (from → to)
	DM_MEMBER( float, lakeFreeboard );
	DM_MEMBER( float2, lakeShallowDepth );
	DM_MEMBER( float2, lakeShorePadding );
	// сила AO по слоям, по четыре, 0…1 (TerrainLayers.occlusion, «Layer occlusion»)
	DM_MEMBER_ARRAY( float4, layerOcclusion, TERRAIN_SPLAT_SLICES );
};
DM_STRUCT_END

#endif
