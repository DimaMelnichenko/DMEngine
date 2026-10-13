////////////////////////////////////////////////////////////////////////////////
// Слои материала террейна — общее для пиксельного шейдера (terrain.ps) и вершинного (cdlod.vs, дисплейсмент): веса восьми
// слоёв (splat-карта и детальный слой весов у русел и мазков), четыре самых весомых, проекции triplanar, смешивание по
// высоте и смещение вершины по высоте слоёв (дисплейсмент, docs/terrain.md, «Дисплейсмент»)
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_LAYERS_SH
#define TERRAIN_LAYERS_SH

#include "cdlod.sh"
#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture2DArray, g_splatMap, 1 );		// срез 0 — веса слоёв 0…3, срез 1 — 4…7, срез 2 — не веса: R — мокрость
DM_SRV( Texture2DArray, g_albedoHeight, 2 );	// альбедо RGB, высота A (0…1 — 1-й…99-й перцентиль карты высоты слоя)
// Детальный слой весов (TerrainDetailSplat): плитки 0,25 м у русел и мазков по сетке детальной земли, с полем в тексель;
// срез 3s — веса слоёв 0…3 места s, 3s + 1 — 4…7, 3s + 2 — мокрость. Индекс: плитка (x, z) мира → место + 1, 0 — нет
DM_SRV( Texture2DArray, g_splatDetail, 5 );
DM_SRV( Texture2D<uint>, g_splatDetailIndex, 6 );

static const uint maxLayers = 8;
// Слоёв с выборкой текстур в пикселе: больше трёх-четырёх в одной точке не бывает, а массив из восьми LayerSample
// занял бы регистры и снизил занятость GPU
static const uint activeLayers = 4;
static const float minWeight = 0.01f;	// слой или проекцию с меньшим вкладом не читаем

// Детальный слой в точке (0,25 м): веса восьми слоёв (галька русел по врезу и дно под водой уже в них) и срез не весов
// (x — мокрость от воды static); false — плитки нет
bool sampleSplatDetail( float2 worldXZ, out float4 weights0, out float4 weights1, out float4 extra )
{
	weights0 = 0.0f;
	weights1 = 0.0f;
	extra = 0.0f;
	if( g_splatDetailTile <= 0.0f )
		return false;
	uint tilesX, tilesZ;
	g_splatDetailIndex.GetDimensions( tilesX, tilesZ );
	const int2 tile = int2( floor( worldXZ / g_splatDetailTile ) );
	if( any( tile < 0 ) || tile.x >= (int)tilesX || tile.y >= (int)tilesZ )
		return false;
	const uint index = g_splatDetailIndex.Load( int3( tile, 0 ) );
	if( index == 0 )
		return false;
	uint width, height, slices;
	g_splatDetail.GetDimensions( width, height, slices );
	// Тексель i плитки — центр в tile + ( i − 1 + 0,5 ) · cell: поле в тексель с каждой стороны
	const float cell = g_splatDetailTile / ( width - 2 );
	const float2 uv = ( ( worldXZ - tile * g_splatDetailTile ) / cell + 1.0f ) / float2( width, height );
	const float slice = 3.0f * ( index - 1 );
	weights0 = g_splatDetail.SampleLevel( g_SamplerLinearClamp, float3( uv, slice ), 0.0f );
	weights1 = g_splatDetail.SampleLevel( g_SamplerLinearClamp, float3( uv, slice + 1.0f ), 0.0f );
	extra = g_splatDetail.SampleLevel( g_SamplerLinearClamp, float3( uv, slice + 2.0f ), 0.0f );
	return true;
}

// Веса проекций triplanar по нормали рельефа; малые отбрасываются
float3 projectionWeights( float3 normal )
{
	float3 weights = pow( abs( normal ), g_triplanarSharpness );
	weights /= dot( weights, 1.0f );
	weights *= step( minWeight, weights );
	return weights / dot( weights, 1.0f );
}

// Четыре самых весомых слоя по убыванию веса, веса нормированы; слои, которых нет в TerrainLayers, веса не получают
void selectLayers( float weights[maxLayers], out float4 splat, out uint4 layerIndex )
{
	splat = 0.0f;
	layerIndex = 0;
	[unroll] for( uint i = 0; i < maxLayers; ++i )
	{
		float weight = i < g_layerCount ? weights[i] : 0.0f;
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
// прибавляется высота его текстуры, и виден только тот слой, чья сумма не ниже максимума больше чем на g_heightBlendDepth
float4 heightBlend( float4 splat, float4 heights )
{
	float4 level = splat + heights;
	float threshold = max( max( level.x, level.y ), max( level.z, level.w ) ) - g_heightBlendDepth;
	float4 blend = max( level - threshold, 0.0f ) * step( minWeight, splat );
	return blend / max( dot( blend, 1.0f ), 1e-5f );
}

// Высота слоя в точке по проекциям triplanar (как addProjections в terrain.ps), мип — шагом spacing, м: без производных
// (вершинный шейдер), мип по шагу сетки — мельче её высота дала бы шум
float layerHeight( uint layer, float3 position, float3 projection, float spacing )
{
	const float scale = g_layerScale[layer / 4][layer % 4];
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
// (высота − 0,5) · размах слоя. distance — до точки LOD вида: полное ближе g_displacementStart, к g_displacementEnd — нет.
// Всё — функция положения и расстояния: у общих вершин соседних уровней и в видах теней смещение одно, трещин нет
float terrainDisplacement( float3 position, float3 normal, float distance )
{
	const float fade = 1.0f - smoothstep( g_displacementStart, g_displacementEnd, distance );
	[branch] if( fade <= 0.0f )
		return 0.0f;
	const float2 uv = heightMapUV( position.xz );
	const float4 splat0 = g_splatMap.SampleLevel( g_SamplerLinearClamp, float3( uv, 0.0f ), 0.0f );
	const float4 splat1 = g_splatMap.SampleLevel( g_SamplerLinearClamp, float3( uv, 1.0f ), 0.0f );
	float weights[maxLayers] = { splat0.x, splat0.y, splat0.z, splat0.w, splat1.x, splat1.y, splat1.z, splat1.w };
	// У русел и мазков — детальный слой весов (в пределах дисплейсмента пиксель всегда мельче текселя карты: плитка целиком)
	float4 fine0, fine1, extra;
	[branch] if( sampleSplatDetail( position.xz, fine0, fine1, extra ) )
	{
		const float fine[maxLayers] = { fine0.x, fine0.y, fine0.z, fine0.w, fine1.x, fine1.y, fine1.z, fine1.w };
		[unroll] for( uint layer = 0; layer < maxLayers; ++layer )
			weights[layer] = fine[layer];
	}
	float4 splat;
	uint4 layerIndex;
	selectLayers( weights, splat, layerIndex );

	// Шаг сетки у вершины — от 6,25 см у камеры, мип высоты слоя — по нему
	const float spacing = max( 0.0625f, distance / 128.0f );
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
		displacement += blend[n] * ( heights[n] - 0.5f ) * g_layerDisplacement[layerIndex[n] / 4][layerIndex[n] % 4];
	return displacement * fade;
}

#endif
