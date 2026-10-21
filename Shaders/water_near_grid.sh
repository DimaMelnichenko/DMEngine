////////////////////////////////////////////////////////////////////////////////
// Сетка окна у камеры (NearWaterFlow, docs/water.md, «Подробное течение у камеры») — общее у течения под «жёсткой
// крышкой» (water_flow.cs с FLOW_NEAR) и мелкой воды (water_near_shallow.cs): адрес ячейки по модулю стороны окна, детальная
// вода и земля под точкой мира, верх камней русла. Перед включением — water_simulation.sh (g_heightMultiplier,
// g_heightOffset)
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_NEAR_GRID_SH
#define WATER_NEAR_GRID_SH

#include "water_detail_flow_constants.h"
#include "water_near_flow_constants.h"

DM_SRV( Texture2DArray<float4>, g_detailStatic, 3 );		// детальные плитки: уровень воды (−1e9 — нет), u₀ по X и Z мира, R русла
DM_SRV( Texture2DArray<float2>, g_terrainDetailTiles, 6 );	// детальная земля: нормированная высота, врез
DM_SRV( Texture2D<uint>, g_detailIndex, 7 );				// плитка (x, z) мира → срез + 1, 0 — плитки нет
// Камни русла в окне — по два: угол квадрата верха (x, z мира), его сторона, низ камня, м; номер квадрата (asuint x)
DM_SRV( StructuredBuffer<float4>, g_nearStones, 9 );
// Верх камней (StoneTop, Scatterer): квадраты WATER_NEAR_STONE_CELLS² высот над низом камня, м, по две половинной точности
// в uint (младшая — чётный тексель); меньше 0 — меша нет
DM_SRV( StructuredBuffer<uint>, g_nearStoneTops, 8 );
DM_SRV( Texture2DArray<float4>, g_detailWater, 10 );		// детальная вода (SLOT_WATER_DETAIL): уровень, течение X, Z, глубина

// Ячейка мира (шаг g_nearStep от угла мира) → тексель окна по модулю его стороны: окно сдвигается, а ячейка остаётся в
// том же текселе; false — ячейка вне окна
bool cellAddress( int2 cell, out uint2 address )
{
	const int size = (int)g_nearSize;
	address = uint2( ( cell % size + size ) % size );
	const int2 local = cell - g_nearOrigin;
	return all( local >= 0 ) && all( local < size );
}

// Тексель детальной плитки под точкой мира, м: x, y — дробные в текселях плитки (с полем в тексель), z — срез; false —
// плитки нет
bool detailTexel( float2 world, out float3 texel )
{
	texel = 0.0f;
	const float2 grid = world / g_detailStep;
	const int2 tile = int2( floor( grid / (float)g_detailCells ) );
	if( any( tile < 0 ) || any( tile >= (int)g_detailTilesPerSide ) )
		return false;
	const uint slice = g_detailIndex.Load( int3( tile, 0 ) );
	texel = float3( grid - float2( tile ) * (float)g_detailCells + 1.0f, (float)slice - 1.0f );
	return slice != 0;
}

// Четыре текселя вокруг точки (дробный тексель) и билинейные веса
float bilinearWeight( float2 t, int2 offset )
{
	return ( offset.x ? t.x : 1.0f - t.x ) * ( offset.y ? t.y : 1.0f - t.y );
}

// Статичная вода плиток в точке мира — билинейно по текселям с водой: уровень, u₀ по X и Z мира, R; false — воды нет
bool detailStaticAt( float2 world, out float4 value )
{
	value = 0.0f;
	float3 texel;
	if( !detailTexel( world, texel ) )
		return false;
	const float2 grid = texel.xy - 0.5f;
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float4 sum = 0.0f;
	float weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		const float weight = bilinearWeight( t, offset );
		const float4 sample = g_detailStatic.Load( int4( c + offset, texel.z, 0 ) );
		if( weight <= 0.0f || sample.x <= -1e8f )
			continue;
		sum += weight * sample;
		weights += weight;
	}
	if( weights <= 1e-6f )
		return false;
	value = sum / weights;
	return true;
}

// Детальная вода в точке мира — билинейно по мокрым текселям: x — уровень, y, z — течение по X и Z мира, w — глубина;
// false — воды нет
bool detailWaterAt( float2 world, out float4 value )
{
	value = 0.0f;
	float3 texel;
	if( !detailTexel( world, texel ) )
		return false;
	const float2 grid = texel.xy - 0.5f;
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float4 sum = 0.0f;
	float weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		const float weight = bilinearWeight( t, offset );
		const float4 sample = g_detailWater.Load( int4( c + offset, texel.z, 0 ) );
		if( weight <= 0.0f || sample.w < g_detailWetDepth )
			continue;
		sum += weight * sample;
		weights += weight;
	}
	if( weights <= 1e-6f )
		return false;
	value = sum / weights;
	return true;
}

// Детальная земля в точке мира, м (билинейно); −1e9 — плитки нет
float detailBed( float2 world )
{
	float3 texel;
	if( !detailTexel( world, texel ) )
		return -1e9f;
	const float2 grid = texel.xy - 0.5f;
	const int2 c = int2( floor( grid ) );
	const float2 t = grid - c;
	float height = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = int2( k & 1, k >> 1 );
		height += bilinearWeight( t, offset ) * g_terrainDetailTiles.Load( int4( c + offset, texel.z, 0 ) ).x;
	}
	return height * g_heightMultiplier + g_heightOffset;
}

// Окно вложено в детальную воду, а за краем набора детальных плиток вода — сетки 1 м, окно её не считает (ячейки там —
// суша, у края течение окна стоит). Итог окна к такому краю на этой полосе, м, переходит в детальную воду — как к краю
// окна (nearWaterFade в water_near.sh): без перехода окно обрывалось на краю плитки посреди пруда прямой линией (Dimok,
// 2026-10-21). Считается в расчёте окна раз на ячейку, а не в шейдерах поверхности на каждую выборку (+0,15 мс прохода воды)
static const float nearDetailEdgeFade = 1.0f;

// Доля окна у края набора детальных плиток в точке мира: 1, к краю плитки, за которым плитки нет, — до 0 на полосе
// nearDetailEdgeFade
float nearDetailEdge( float2 world )
{
	const float tileSize = g_detailCells * g_detailStep;
	const int2 tile = int2( floor( world / tileSize ) );
	const float2 local = world - tile * tileSize;
	float weight = 1.0f;
	[unroll] for( int k = 0; k < 4; ++k )
	{
		const int2 side = k == 0 ? int2( -1, 0 ) : k == 1 ? int2( 1, 0 ) : k == 2 ? int2( 0, -1 ) : int2( 0, 1 );
		const float distance = k == 0 ? local.x : k == 1 ? tileSize - local.x : k == 2 ? local.y : tileSize - local.y;
		if( distance >= nearDetailEdgeFade )
			continue;
		const int2 neighbor = tile + side;
		const bool present = all( neighbor >= 0 ) && all( neighbor < (int)g_detailTilesPerSide ) &&
							 g_detailIndex.Load( int3( neighbor, 0 ) ) != 0;
		if( !present )
			weight = min( weight, distance / nearDetailEdgeFade );
	}
	return saturate( weight );
}

// Верх камней русла окна в точке мира (квадраты StoneTop: тексель под точкой), не ниже bed, м
float nearStoneTop( float2 world, float bed )
{
	float top = bed;
	[loop] for( uint s = 0; s < g_nearStoneCount; ++s )
	{
		const float4 stone = g_nearStones[2 * s];
		const int2 texel = int2( floor( ( world - stone.xy ) / stone.z * WATER_NEAR_STONE_CELLS ) );
		if( any( texel < 0 ) || any( texel >= WATER_NEAR_STONE_CELLS ) )
			continue;
		const uint index = ( asuint( g_nearStones[2 * s + 1].x ) * WATER_NEAR_STONE_CELLS + texel.y ) * WATER_NEAR_STONE_CELLS + texel.x;
		const uint word = g_nearStoneTops[index >> 1];
		const float height = f16tof32( ( index & 1 ) != 0 ? word >> 16 : word & 0xffff );
		if( height >= 0.0f )
			top = max( top, stone.w + height );
	}
	return top;
}

#endif
