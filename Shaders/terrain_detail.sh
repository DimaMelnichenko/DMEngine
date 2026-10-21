////////////////////////////////////////////////////////////////////////////////
// Детальная земля у русел (TerrainPipeline, docs/terrain.md, «Детальная земля»): плитки высот по сетке мира со стороной
// tileSize (лист квадродерева, 32 м) с шагом мельче карты высот (0,25 м) — там, где проходит русло. Массив
// плиток (SLOT_TERRAIN_DETAIL): срез — плитка, тексели по x и z мира (строка растёт с z), с полем в тексель с каждой
// стороны — билинейная выборка у края плитки не выходит за срез; x — высота, нормированная, как карта высот
// (× heightMultiplier + heightOffset), y — врез русла или чаши озера, м (покраска галькой, очистка растительности). Индекс (SLOT_TERRAIN_DETAIL_INDEX): плитка (x, z) мира → номер среза + 1, 0 —
// плитки нет. Вершина и точка берут плитку по своему положению — у общей вершины соседних патчей она одна и та же
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_DETAIL_SH
#define TERRAIN_DETAIL_SH

#include "slots.h"
#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture2DArray<float2>, g_terrainDetail, SLOT_TERRAIN_DETAIL );
DM_SRV( Texture2D<uint>, g_terrainDetailIndex, SLOT_TERRAIN_DETAIL_INDEX );

// Детальная плитка в точке: x — нормированная высота, y — врез, м; false — плитки здесь нет (tileSize ≤ 0 — детальной
// земли нет совсем)
bool sampleTerrainDetail2( float2 worldXZ, float tileSize, out float2 value )
{
	value = 0.0f;
	if( tileSize <= 0.0f )
		return false;
	uint tilesX, tilesZ;
	g_terrainDetailIndex.GetDimensions( tilesX, tilesZ );
	const int2 tile = int2( floor( worldXZ / tileSize ) );
	if( any( tile < 0 ) || tile.x >= (int)tilesX || tile.y >= (int)tilesZ )
		return false;
	const uint index = g_terrainDetailIndex.Load( int3( tile, 0 ) );
	if( index == 0 )
		return false;
	uint width, height, slices;
	g_terrainDetail.GetDimensions( width, height, slices );
	// Тексель i плитки — центр в tile + ( i − 1 + 0,5 ) · cell: поле в тексель с каждой стороны
	const float cell = tileSize / ( width - 2 );
	const float2 texel = ( worldXZ - tile * tileSize ) / cell + 1.0f;
	value = g_terrainDetail.SampleLevel( g_SamplerLinearClamp, float3( texel / float2( width, height ), index - 1 ), 0.0f );
	return true;
}

// Доля у края набора детальных плиток в точке: 1, к краю плитки, за которым плитки нет, — до 0 на полосе fade, м
float detailEdgeWeight( float2 worldXZ, float tileSize, float fade )
{
	uint tilesX, tilesZ;
	g_terrainDetailIndex.GetDimensions( tilesX, tilesZ );
	const int2 tile = int2( floor( worldXZ / tileSize ) );
	const float2 local = worldXZ - tile * tileSize;
	float weight = 1.0f;
	[unroll] for( int k = 0; k < 4; ++k )
	{
		const int2 side = k == 0 ? int2( -1, 0 ) : k == 1 ? int2( 1, 0 ) : k == 2 ? int2( 0, -1 ) : int2( 0, 1 );
		const float distance = k == 0 ? local.x : k == 1 ? tileSize - local.x : k == 2 ? local.y : tileSize - local.y;
		if( distance >= fade )
			continue;
		const int2 neighbor = tile + side;
		const bool present = all( neighbor >= 0 ) && neighbor.x < (int)tilesX && neighbor.y < (int)tilesZ &&
							 g_terrainDetailIndex.Load( int3( neighbor, 0 ) ) != 0;
		if( !present )
			weight = min( weight, distance / fade );
	}
	return saturate( weight );
}

// Нормированная высота детальной плитки в точке; false — плитки нет
bool sampleTerrainDetail( float2 worldXZ, float tileSize, out float value )
{
	float2 detail;
	const bool found = sampleTerrainDetail2( worldXZ, tileSize, detail );
	value = detail.x;
	return found;
}

#endif
