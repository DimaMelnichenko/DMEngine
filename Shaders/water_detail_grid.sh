////////////////////////////////////////////////////////////////////////////////
// Счёт по плиткам детальной воды и сетке 1 м при загрузке (WaterGrid: поле пены — water_foam.cs, фаза стоячих волн —
// water_standing_waves.cs): ячейка группы потоков, точка мира → тексель детальной воды для выборки, тексели записи —
// своя плитка и поле соседних. Группа потоков — блок 8 × 8 детальной воды (блоки с водой, как у течения) или тайл 8 × 8
// сетки 1 м (тайлы течения). Плитки детальной воды — по x и z мира (строка растёт с z), с полем в тексель с каждой стороны;
// сетка 1 м — строки против Z мира
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_DETAIL_GRID_SH
#define WATER_DETAIL_GRID_SH

#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture2DArray<float4>, g_detailWater, 0 );		// детальная вода: уровень (−1e30 — нет), течение X, Z мира, глубина
DM_SRV( Texture2D<uint>, g_detailIndex, 1 );				// плитка (x, z) мира → срез + 1, 0 — плитки нет
DM_SRV( StructuredBuffer<uint2>, g_detailTileCoords, 2 );	// срез → плитка (x, z) мира
DM_SRV( StructuredBuffer<uint2>, g_groups, 3 );			// детальная сетка — срез и номер блока в плитке; сетка 1 м — тайл
DM_SRV( Texture2D<float4>, g_coarseWater, 4 );			// вода сетки 1 м (SLOT_WATER): глубина, течение X, Z мира

#include "water_grid_constants.h"

float detailStep()
{
	return g_gridTileSize / ( g_gridSide - 2 );
}

// Ячейка детальной сетки потока: тексель среза (с полем), клетка детальной сетки мира, точка мира (x, z)
bool detailCell( uint3 group, uint3 thread, out uint3 texel, out int2 cell, out float2 world )
{
	const uint cells = g_gridSide - 2;
	const uint2 block = g_groups[min( group.x, g_gridGroupCount - 1 )];
	const uint2 local = uint2( block.y % ( cells / 8 ), block.y / ( cells / 8 ) ) * 8 + thread.xy;
	texel = uint3( local + 1, block.x );
	cell = int2( g_detailTileCoords[block.x] * cells + local );
	world = ( cell + 0.5f ) * detailStep();
	return group.x < g_gridGroupCount;
}

// Точка мира → тексель детальной воды для выборки: x, y — непрерывные (центр текселя i — i + 0,5, с полем), z — срез;
// false — плитки нет
bool detailTexelAt( float2 world, out float3 texel )
{
	texel = 0.0f;
	const int2 tile = int2( floor( world / g_gridTileSize ) );
	uint tilesX, tilesZ;
	g_detailIndex.GetDimensions( tilesX, tilesZ );
	if( any( tile < 0 ) || tile.x >= (int)tilesX || tile.y >= (int)tilesZ )
		return false;
	const uint index = g_detailIndex.Load( int3( tile, 0 ) );
	if( index == 0 )
		return false;
	texel = float3( ( world - tile * g_gridTileSize ) / detailStep() + 1.0f, index - 1 );
	return true;
}

float3 detailUV( float3 texel )
{
	return float3( texel.xy / g_gridSide, texel.z );
}

// UV сетки 1 м: строки — против Z мира
float2 coarseUV( float2 world )
{
	return float2( world.x, g_gridWorldSize - world.y ) / g_gridWorldSize;
}

bool wetDetail( float4 detailWater )
{
	return detailWater.w >= g_gridWetDepth && detailWater.x > -1e3f;
}

// Течение в точке мира: детальная вода, вне плиток — сетка 1 м
float2 detailVelocityAt( float2 world )
{
	float3 texel;
	if( detailTexelAt( world, texel ) )
		return g_detailWater.SampleLevel( g_SamplerLinearClamp, detailUV( texel ), 0.0f ).yz;
	return g_coarseWater.SampleLevel( g_SamplerLinearClamp, coarseUV( world ), 0.0f ).yz;
}

// Тексели, куда пишет ячейка детальной сетки: своя плитка и поле соседних (тексель поля — копия ячейки соседа: выборка у
// края плитки — без шва); у угла плитки — до четырёх
uint detailCopies( int2 cell, out uint3 texels[4] )
{
	const int cells = (int)g_gridSide - 2;
	const int2 tile = cell / cells;
	uint tilesX, tilesZ;
	g_detailIndex.GetDimensions( tilesX, tilesZ );
	uint count = 0;
	texels[0] = texels[1] = texels[2] = texels[3] = 0;
	[unroll] for( int k = 0; k < 9; ++k )
	{
		const int2 neighbor = tile + int2( k % 3 - 1, k / 3 - 1 );
		if( any( neighbor < 0 ) || neighbor.x >= (int)tilesX || neighbor.y >= (int)tilesZ )
			continue;
		const uint slice = g_detailIndex.Load( int3( neighbor, 0 ) );
		const int2 local = cell - neighbor * cells;
		if( slice == 0 || any( local < -1 ) || any( local > cells ) || count >= 4 )
			continue;
		texels[count++] = uint3( local + 1, slice - 1 );
	}
	return count;
}

// Клетка сетки 1 м потока и её центр в мире (x, z)
bool coarseCell( uint3 group, uint3 thread, out int2 cell, out float2 world )
{
	cell = int2( g_groups[min( group.x, g_gridGroupCount - 1 )] * 8 + thread.xy );
	world = float2( ( cell.x + 0.5f ) * g_gridCellSize, g_gridWorldSize - ( cell.y + 0.5f ) * g_gridCellSize );
	return group.x < g_gridGroupCount && all( cell < (int)g_gridSize );
}

#endif
