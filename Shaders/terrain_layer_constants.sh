////////////////////////////////////////////////////////////////////////////////
// Константы слоёв материала террейна (terrain_layers.sh): одна раскладка у структурного буфера сцены и у cbuffer террейна
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_LAYER_CONSTANTS_SH
#define TERRAIN_LAYER_CONSTANTS_SH

// CDLODTerrain::LayerConstants — то, что нужно выбору слоёв и дисплейсменту: ресурс сцены SLOT_TERRAIN_LAYER_CONSTANTS
// (структурный буфер из одного элемента — расстановка, частицы) и копия в cbuffer террейна (cdlod.sh)
struct TerrainLayerConstants
{
	float4 layerScale[2];			// по слоям, по четыре: повторов текстуры на единицу мира
	float4 layerDisplacement[2];	// размах смещения по слоям, по четыре, м (TerrainLayers.displacement, «Layer displacement»)
	float3 lodOrigin;				// точка LOD главного вида (у видов теней — та же)
	float  lodEyeHeight;			// ноги ниже точки LOD, м (ходьба; в полёте 0): LOD — от отрезка «глаза — ноги»
	float  worldSize;				// сторона террейна, м
	float  triplanarSharpness;		// чем больше, тем уже переход между проекциями triplanar
	float  heightBlendDepth;		// ширина перехода между слоями при смешивании по высоте
	uint   layerCount;				// слоёв в массивах текстур (TerrainLayers)
	float  splatDetailTile;			// сторона плитки детального слоя весов, м; 0 — плиток нет
	float  displacementStart;		// дисплейсмент: полный ближе, м,
	float  displacementEnd;			// к этому расстоянию от точки LOD — нет; 0 — выключен
	float  padding;
	float4 layerTint[8];			// множитель альбедо по слоям (xyz; TerrainLayers.albedo_tint, окно «Layer tint»)
};

#endif
