////////////////////////////////////////////////////////////////////////////////
// Ядро материала террейна — общее у рельефа карты (terrain.ps) и дальних гор (far_terrain.ps): по весам восьми слоёв —
// четыре самых весомых, их текстуры проекциями triplanar (на крутых склонах ещё вдоль X и Z), вдали — и в крупном масштабе
// (distance resampling), смешивание по высоте, множитель альбедо слоя («Layer tint») и AO слоя с его силой. Слой — пара
// текстур из массивов: альбедо + высота и нормаль (RG) + AO (B) + шероховатость. Откуда веса — решает шейдер: у карты —
// splat-карта и детальный слой весов, у гор — правила по высоте и уклону. Всё — ресурсы и константы сцены (terrain_layers.sh):
// ядро зовут и модели, переходящие в землю (terrain_blend.sh). Шейдеры террейна подключают cdlod.sh раньше — константы слоёв
// у них из своего cbuffer
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_MATERIAL_SH
#define TERRAIN_MATERIAL_SH

#include "frame_constants.h"
#include "terrain_layers.sh"

DM_SRV( Texture2DArray, g_normalRoughness, SLOT_TERRAIN_NORMAL_ROUGHNESS );

struct LayerSample
{
	float3 albedo;
	float  height;
	float3 detail;		// отклонение нормали по карте нормалей, в мировых координатах
	float  roughness;
	float  occlusion;	// затенение окружением слоя (B массива нормалей)
};

// tangent и bitangent — направления роста u и v проекции в мире. Отклонение нормали добавляется к нормали рельефа
// в плоскости проекции (UDN): для плоского участка это обычный normal mapping. stochastic и «Hex cell» — стохастический
// повтор (hex_tiling.sh): обе текстуры у трёх ячеек, производные UV — поворотом ячейки, отклонение нормали — обратно в UV,
// веса ячеек — по высотам их выборок (те же, что у дисплейсмента, layerProjectionHeight)
void addProjection( inout LayerSample result, float weight, uint layer, float2 uv, float2 uvDX, float2 uvDY,
					float3 tangent, float3 bitangent, bool stochastic )
{
	const TerrainLayerConstants layers = terrainLayers();
	float4 albedoHeight = 0.0f;
	float4 normalRoughness = 0.0f;
	float2 slope = 0.0f;
	[branch] if( !stochastic || layers.hexCell <= 0.0f )
	{
		albedoHeight = g_albedoHeight.SampleGrad( g_SamplerAnisotropicWrap, float3( uv, layer ), uvDX, uvDY );
		normalRoughness = g_normalRoughness.SampleGrad( g_SamplerAnisotropicWrap, float3( uv, layer ), uvDX, uvDY );
		slope = normalRoughness.xy * 2.0f - 1.0f;
	}
	else
	{
		const HexCells cells = hexCells( uv, layers.hexCell, layerHexRotation( layer ), layer,
										 hexSkipWeight( layers.hexHeightWeight, layers.hexGain ) );
		float4 cellAlbedo[3];
		float4 cellNormal[3];
		[unroll] for( uint k = 0; k < 3; ++k )
		{
			cellAlbedo[k] = 0.0f;
			cellNormal[k] = 0.5f;
			[branch] if( cells.weight[k] > 0.0f )
			{
				const float2 dx = hexRotate( uvDX, cells.rotation[k] );
				const float2 dy = hexRotate( uvDY, cells.rotation[k] );
				cellAlbedo[k] = g_albedoHeight.SampleGrad( g_SamplerAnisotropicWrap, float3( cells.uv[k], layer ), dx, dy );
				cellNormal[k] = g_normalRoughness.SampleGrad( g_SamplerAnisotropicWrap, float3( cells.uv[k], layer ), dx, dy );
			}
		}
		const float3 w = hexWeights( cells, float3( cellAlbedo[0].a, cellAlbedo[1].a, cellAlbedo[2].a ), layers.hexHeightWeight,
									 layers.hexGain );
		[unroll] for( uint n = 0; n < 3; ++n )
		{
			albedoHeight += cellAlbedo[n] * w[n];
			normalRoughness += cellNormal[n] * w[n];
			slope += hexToUV( cellNormal[n].xy * 2.0f - 1.0f, cells.rotation[n] ) * w[n];
		}
	}

	result.albedo += albedoHeight.rgb * weight;
	result.height += albedoHeight.a * weight;
	result.detail += ( tangent * slope.x + bitangent * slope.y ) * weight;
	result.roughness += normalRoughness.a * weight;
	result.occlusion += normalRoughness.b * weight;
}

// Проекции слоя с повтором scale (повторов на метр), вклад — с весом weight.
// Производные позиции считаются до ветвлений: внутри них неявные производные не определены, поэтому SampleGrad.
// Стохастический повтор — только у верхней проекции: ею покрыты луга и склоны до ~40°, где повтор и складывается в сетку;
// у боковых (обрывы) он удвоил бы цену (склон во весь кадр: +0,48 мс вместо +0,2), а на глаз почти не меняет
void addProjections( inout LayerSample result, float weight, uint layer, float scale, float3 position, float3 positionDX,
					 float3 positionDY, float3 projection )
{
	// Сверху: u вдоль X, v вдоль Z
	[branch] if( projection.y > 0.0f )
	{
		addProjection( result, projection.y * weight, layer, position.xz * scale, positionDX.xz * scale, positionDY.xz * scale,
					   float3( 1.0f, 0.0f, 0.0f ), float3( 0.0f, 0.0f, 1.0f ), true );
	}
	// Вдоль X: u вдоль Z, v вниз по Y, чтобы верх текстуры смотрел вверх
	[branch] if( projection.x > 0.0f )
	{
		addProjection( result, projection.x * weight, layer, float2( position.z, -position.y ) * scale,
					   float2( positionDX.z, -positionDX.y ) * scale, float2( positionDY.z, -positionDY.y ) * scale,
					   float3( 0.0f, 0.0f, 1.0f ), float3( 0.0f, -1.0f, 0.0f ), false );
	}
	// Вдоль Z: u вдоль X, v вниз по Y
	[branch] if( projection.z > 0.0f )
	{
		addProjection( result, projection.z * weight, layer, float2( position.x, -position.y ) * scale,
					   float2( positionDX.x, -positionDX.y ) * scale, float2( positionDY.x, -positionDY.y ) * scale,
					   float3( 1.0f, 0.0f, 0.0f ), float3( 0.0f, -1.0f, 0.0f ), false );
	}
}

// far — доля крупного масштаба (0 — вблизи, 1 — вдали): обе выборки только в полосе перехода; повтор обоих — ещё крупнее
// в scaleDivider раз (дальние горы вдали — третий масштаб)
LayerSample sampleLayer( uint layer, float far, float scaleDivider, float3 position, float3 positionDX, float3 positionDY,
						 float3 projection )
{
	LayerSample result = (LayerSample)0;
	const float scale = layerScale( layer ) / scaleDivider;
	[branch] if( far < 1.0f )
		addProjections( result, 1.0f - far, layer, scale, position, positionDX, positionDY, projection );
	[branch] if( far > 0.0f )
		addProjections( result, far, layer, scale / terrainLayers().farTextureScale, position, positionDX, positionDY, projection );
	return result;
}

// Доля крупного масштаба текстур по расстоянию до камеры («Far blend»)
float farTextureShare( float3 position )
{
	const TerrainLayerConstants layers = terrainLayers();
	return saturate( ( length( position - cb_cameraPosition ) - layers.farBlendStart ) / ( layers.farBlendEnd - layers.farBlendStart ) );
}

// Смесь слоёв в точке: альбедо с множителем слоя, отклонение нормали, шероховатость, AO слоёв с их силой, высота текстур
// (0…1, по долям смешивания — рваный край перехода моделей в землю)
struct TerrainLayerMix
{
	float3 albedo;
	float3 detail;
	float  roughness;
	float  occlusion;
	float  height;
};

// weights — веса восьми слоёв (не обязательно нормированные), far — доля крупного масштаба (farTextureShare), scaleDivider —
// во сколько раз крупнее повтор (1 — как задан), projection — веса проекций triplanar по нормали рельефа (projectionWeights)
TerrainLayerMix mixTerrainLayers( float weights[maxLayers], float far, float scaleDivider, float3 position, float3 positionDX,
								  float3 positionDY, float3 projection )
{
	float4 splat;
	uint4 layerIndex;
	selectLayers( weights, splat, layerIndex );

	LayerSample layers[activeLayers];
	float4 heights = 0.0f;
	[unroll] for( uint j = 0; j < activeLayers; ++j )
	{
		layers[j] = (LayerSample)0;
		[branch] if( splat[j] > minWeight )
		{
			layers[j] = sampleLayer( layerIndex[j], far, scaleDivider, position, positionDX, positionDY, projection );
		}
		heights[j] = layers[j].height;
	}

	float4 blend = heightBlend( splat, heights );
	TerrainLayerMix mixed = (TerrainLayerMix)0;
	mixed.occlusion = 1.0f;
	[unroll] for( uint n = 0; n < activeLayers; ++n )
	{
		mixed.albedo += layers[n].albedo * terrainLayers().layerTint[layerIndex[n]].xyz * blend[n];
		mixed.detail += layers[n].detail * blend[n];
		mixed.roughness += layers[n].roughness * blend[n];
		mixed.height += layers[n].height * blend[n];
		// AO слоя с его силой, смешанный теми же долями, что цвет
		mixed.occlusion -= ( 1.0f - layers[n].occlusion ) * terrainLayers().layerOcclusion[layerIndex[n] / 4][layerIndex[n] % 4] * blend[n];
	}
	return mixed;
}

#endif
