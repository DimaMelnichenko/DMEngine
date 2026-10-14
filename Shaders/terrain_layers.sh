////////////////////////////////////////////////////////////////////////////////
// Слои материала террейна — общее для всех, кто читает землю: пиксельный (terrain.ps) и вершинный (cdlod.vs,
// дисплейсмент) шейдеры террейна, расстановка и частицы (terrain_height.sh — земля со смещением). Веса восьми слоёв
// (splat-карта и детальный слой весов у русел и мазков), четыре самых весомых, проекции triplanar, смешивание по высоте,
// расстояние LOD и смещение вершины по высоте слоёв (дисплейсмент, docs/terrain.md, «Дисплейсмент»). Всё — ресурсы сцены
// (CDLODTerrain::compute); без террейна слоты пусты: читать их можно только у уровня с террейном. Шейдеры террейна берут
// константы из своего cbuffer (TERRAIN_LAYERS_IN_CBUFFER, cdlod.sh): у каждой вершины и пикселя он дешевле буфера
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_LAYERS_SH
#define TERRAIN_LAYERS_SH

#include "slots.h"
#include "samplers.sh"
#include "bindless.sh"
#include "terrain_layer_constants.sh"

#ifndef TERRAIN_LAYERS_IN_CBUFFER
DM_SRV( StructuredBuffer<TerrainLayerConstants>, g_terrainLayerConstants, SLOT_TERRAIN_LAYER_CONSTANTS );
#endif
DM_SRV( Texture2DArray, g_splatMap, SLOT_TERRAIN_SPLAT );			// срез 0 — веса слоёв 0…3, срез 1 — 4…7, срез 2 — не веса: R — мокрость
DM_SRV( Texture2DArray, g_albedoHeight, SLOT_TERRAIN_ALBEDO_HEIGHT );	// альбедо RGB, высота A (0…1 — 1-й…99-й перцентиль карты высоты слоя)
// Детальный слой весов (TerrainDetailSplat): плитки 0,25 м у русел и мазков по сетке детальной земли, с полем в тексель;
// срез 3s — веса слоёв 0…3 места s, 3s + 1 — 4…7, 3s + 2 — мокрость. Индекс: плитка (x, z) мира → место + 1, 0 — нет
DM_SRV( Texture2DArray, g_splatDetail, SLOT_TERRAIN_SPLAT_DETAIL );
DM_SRV( Texture2D<uint>, g_splatDetailIndex, SLOT_TERRAIN_SPLAT_DETAIL_INDEX );
DM_SRV( Texture2D, g_terrainNoise, SLOT_TERRAIN_NOISE );	// белый шум 256², R8_SNORM — пятна яркости земли

// Константы слоёв: читаются там, где нужны (без террейна слот пуст — не читать)
TerrainLayerConstants terrainLayers()
{
#ifdef TERRAIN_LAYERS_IN_CBUFFER
	return g_terrainLayers;
#else
	return g_terrainLayerConstants[0];
#endif
}

static const uint maxLayers = 8;
// Слоёв с выборкой текстур в пикселе: больше трёх-четырёх в одной точке не бывает, а массив из восьми LayerSample
// занял бы регистры и снизил занятость GPU
static const uint activeLayers = 4;
static const float minWeight = 0.01f;	// слой или проекцию с меньшим вкладом не читаем

// UV splat-карты (и карты высот): ось z мира идёт по текстуре снизу вверх
float2 terrainLayerUV( float2 worldXZ )
{
	float2 uv = worldXZ / terrainLayers().worldSize;
	uv.y = 1.0f - uv.y;
	return uv;
}

// Расстояние LOD: от отрезка «глаза — ноги» под точкой LOD — земля под ногами рядом, а не в росте от глаз: самая мелкая
// сетка лежит вокруг ног. Так же узлы отбирает CDLODTerrain (lodRangeHitsBox); функция расстояния до выпуклого множества —
// у соседних уровней морфинг сходится, как от точки
float lodDistance( float3 position )
{
	const TerrainLayerConstants layers = terrainLayers();
	const float3 offset = position - layers.lodOrigin;
	const float vertical = max( max( offset.y, -offset.y - layers.lodEyeHeight ), 0.0f );
	return length( float3( offset.x, vertical, offset.z ) );
}

// Детальный слой в точке (0,25 м): веса восьми слоёв (галька русел по врезу и дно под водой уже в них) и срез не весов
// (x — мокрость от воды static); false — плитки нет
bool sampleSplatDetail( float2 worldXZ, out float4 weights0, out float4 weights1, out float4 extra )
{
	weights0 = 0.0f;
	weights1 = 0.0f;
	extra = 0.0f;
	const float tileSize = terrainLayers().splatDetailTile;
	if( tileSize <= 0.0f )
		return false;
	uint tilesX, tilesZ;
	g_splatDetailIndex.GetDimensions( tilesX, tilesZ );
	const int2 tile = int2( floor( worldXZ / tileSize ) );
	if( any( tile < 0 ) || tile.x >= (int)tilesX || tile.y >= (int)tilesZ )
		return false;
	const uint index = g_splatDetailIndex.Load( int3( tile, 0 ) );
	if( index == 0 )
		return false;
	uint width, height, slices;
	g_splatDetail.GetDimensions( width, height, slices );
	// Тексель i плитки — центр в tile + ( i − 1 + 0,5 ) · cell: поле в тексель с каждой стороны
	const float cell = tileSize / ( width - 2 );
	const float2 uv = ( ( worldXZ - tile * tileSize ) / cell + 1.0f ) / float2( width, height );
	const float slice = 3.0f * ( index - 1 );
	weights0 = g_splatDetail.SampleLevel( g_SamplerLinearClamp, float3( uv, slice ), 0.0f );
	weights1 = g_splatDetail.SampleLevel( g_SamplerLinearClamp, float3( uv, slice + 1.0f ), 0.0f );
	extra = g_splatDetail.SampleLevel( g_SamplerLinearClamp, float3( uv, slice + 2.0f ), 0.0f );
	return true;
}

// Веса проекций triplanar по нормали рельефа; малые отбрасываются
float3 projectionWeights( float3 normal )
{
	float3 weights = pow( abs( normal ), terrainLayers().triplanarSharpness );
	weights /= dot( weights, 1.0f );
	weights *= step( minWeight, weights );
	return weights / dot( weights, 1.0f );
}

// Четыре самых весомых слоя по убыванию веса, веса нормированы; слои, которых нет в TerrainLayers, веса не получают
void selectLayers( float weights[maxLayers], out float4 splat, out uint4 layerIndex )
{
	const uint layerCount = terrainLayers().layerCount;
	splat = 0.0f;
	layerIndex = 0;
	[unroll] for( uint i = 0; i < maxLayers; ++i )
	{
		float weight = i < layerCount ? weights[i] : 0.0f;
		uint layer = i;
		[unroll] for( uint k = 0; k < activeLayers; ++k )
		{
			if( weight > splat[k] )
			{
				const float lighter = splat[k];
				const uint lighterLayer = layerIndex[k];
				splat[k] = weight;
				layerIndex[k] = layer;
				weight = lighter;
				layer = lighterLayer;
			}
		}
	}
	splat /= max( dot( splat, 1.0f ), 1e-5f );
}

// Смешивание по высоте (A. Mishkinis, «Advanced Terrain Texture Splatting»): к весу слоя из splat-карты
// прибавляется высота его текстуры, и виден только тот слой, чья сумма не ниже максимума больше чем на heightBlendDepth
float4 heightBlend( float4 splat, float4 heights )
{
	float4 level = splat + heights;
	float threshold = max( max( level.x, level.y ), max( level.z, level.w ) ) - terrainLayers().heightBlendDepth;
	float4 blend = max( level - threshold, 0.0f ) * step( minWeight, splat );
	return blend / max( dot( blend, 1.0f ), 1e-5f );
}

// Крупные пятна яркости земли (тексели шума по 40 и 13 м: вдали не заметно повторение текстур) — множитель альбедо, общий
// у террейна и растений на нём (scatter.cs: над светлым пятном и трава светлее)
float terrainBrightness( float2 worldXZ )
{
	const float variation = g_terrainNoise.SampleLevel( g_SamplerLinearWrap, worldXZ / 10240.0f, 0.0f ).r * 0.6f +
							g_terrainNoise.SampleLevel( g_SamplerLinearWrap, worldXZ / 3328.0f, 0.0f ).r * 0.4f;
	return 1.0f + 0.15f * variation;
}

// Веса четырёх самых весомых слоёв в точке: splat-карта, у русел и мазков — детальный слой весов (плитка целиком: те, кто
// его читает, — у камеры, пиксель мельче текселя карты)
void terrainLayerWeights( float2 worldXZ, out float4 splat, out uint4 layerIndex )
{
	const float2 uv = terrainLayerUV( worldXZ );
	const float4 splat0 = g_splatMap.SampleLevel( g_SamplerLinearClamp, float3( uv, 0.0f ), 0.0f );
	const float4 splat1 = g_splatMap.SampleLevel( g_SamplerLinearClamp, float3( uv, 1.0f ), 0.0f );
	float weights[maxLayers] = { splat0.x, splat0.y, splat0.z, splat0.w, splat1.x, splat1.y, splat1.z, splat1.w };
	float4 fine0, fine1, extra;
	[branch] if( sampleSplatDetail( worldXZ, fine0, fine1, extra ) )
	{
		const float fine[maxLayers] = { fine0.x, fine0.y, fine0.z, fine0.w, fine1.x, fine1.y, fine1.z, fine1.w };
		[unroll] for( uint layer = 0; layer < maxLayers; ++layer )
			weights[layer] = fine[layer];
	}
	selectLayers( weights, splat, layerIndex );
}

// Цвет земли в точке, каким его видит издали тот, кто на ней стоит: средний цвет слоёв (последний мип массива альбедо —
// 1 × 1) по весам, с множителем слоя («Layer tint») и пятнами яркости, линейный. Его берут растения у корня (scatter.cs):
// перекрасили слой — трава подстроилась, рисовать ничего не нужно
float3 terrainGroundColor( float2 worldXZ )
{
	float4 splat;
	uint4 layerIndex;
	terrainLayerWeights( worldXZ, splat, layerIndex );
	uint width, height, slices, mips;
	g_albedoHeight.GetDimensions( 0, width, height, slices, mips );
	const TerrainLayerConstants layers = terrainLayers();
	float3 color = 0.0f;
	[unroll] for( uint k = 0; k < activeLayers; ++k )
	{
		[branch] if( splat[k] > minWeight )
			color += splat[k] * layers.layerTint[layerIndex[k]].xyz *
					 g_albedoHeight.SampleLevel( g_SamplerLinearClamp, float3( 0.5f, 0.5f, layerIndex[k] ), mips - 1 ).rgb;
	}
	return color * terrainBrightness( worldXZ );
}

// Повторов текстуры слоя на метр
float layerScale( uint layer )
{
	return terrainLayers().layerScale[layer / 4][layer % 4];
}

// Высота слоя в точке по проекциям triplanar (как addProjections в terrain.ps), мип — шагом spacing, м: без производных
// (вершинный и compute-шейдеры), мип по шагу сетки — мельче её высота дала бы шум
float layerHeight( uint layer, float3 position, float3 projection, float spacing )
{
	const float scale = layerScale( layer );
	uint width, height, slices;
	g_albedoHeight.GetDimensions( width, height, slices );
	const float lod = max( log2( spacing * scale * width ), 0.0f );
	float result = 0.0f;
	[branch] if( projection.y > 0.0f )
		result += projection.y * g_albedoHeight.SampleLevel( g_SamplerLinearWrap, float3( position.xz * scale, layer ), lod ).a;
	[branch] if( projection.x > 0.0f )
		result += projection.x * g_albedoHeight.SampleLevel( g_SamplerLinearWrap, float3( float2( position.z, -position.y ) * scale, layer ), lod ).a;
	[branch] if( projection.z > 0.0f )
		result += projection.z * g_albedoHeight.SampleLevel( g_SamplerLinearWrap, float3( float2( position.x, -position.y ) * scale, layer ), lod ).a;
	return result;
}

// Дисплейсмент (как Nanite-тесселяция Landscape в UE5): смещение вершины вдоль нормали, м — высоты тех же четырёх слоёв и
// то же смешивание по высоте, что видит пиксель (камни поднимаются там, где нарисованы), вокруг середины высоты: Σ blend ·
// (высота − 0,5) · размах слоя. distance — lodDistance: полное ближе displacementStart, к displacementEnd — нет.
// Всё — функция положения и расстояния: у общих вершин соседних уровней, в видах теней и у тех, кто стоит на земле
// (terrainSurfacePoint), смещение одно
float terrainDisplacement( float3 position, float3 normal, float distance )
{
	const TerrainLayerConstants layers = terrainLayers();
	const float fade = 1.0f - smoothstep( layers.displacementStart, layers.displacementEnd, distance );
	[branch] if( fade <= 0.0f )
		return 0.0f;
	float4 splat;
	uint4 layerIndex;
	terrainLayerWeights( position.xz, splat, layerIndex );

	// Шаг сетки у вершины — от 1,56 см у камеры (около ⅛ расстояния / 16, как диапазоны уровней), мип высоты слоя — по нему
	const float spacing = max( 0.015625f, distance / 128.0f );
	const float3 projection = projectionWeights( normal );
	float4 heights = 0.0f;
	[unroll] for( uint j = 0; j < activeLayers; ++j )
	{
		[branch] if( splat[j] > minWeight )
			heights[j] = layerHeight( layerIndex[j], position, projection, spacing );
	}
	const float4 blend = heightBlend( splat, heights );
	float displacement = 0.0f;
	[unroll] for( uint n = 0; n < activeLayers; ++n )
		displacement += blend[n] * ( heights[n] - 0.5f ) * layers.layerDisplacement[layerIndex[n] / 4][layerIndex[n] % 4];
	return displacement * fade;
}

#endif
