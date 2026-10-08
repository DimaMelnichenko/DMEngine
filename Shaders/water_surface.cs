////////////////////////////////////////////////////////////////////////////////
// Поверхность воды: что рисовать и где (класс WaterSimulation, docs/water.md). Сетка поверхности — центры ячеек
// симуляции, тайлами по WATER_TILE ячеек:
//   mainSurface    — после шагов симуляции: уровень воды в ячейке (рельеф + глубина) там, где воды видно (глубже
//                    g_visibleDepth). У сухой ячейки рядом с водой — средний уровень мокрых соседей: плоскость воды
//                    продолжается за урез и уходит под рельеф, берег — линия их пересечения (её даёт проверка
//                    глубины), плавно внутри ячейки, а не по её границе. Остальное сухое — без уровня; глубина < 0 —
//                    воду ячейки рисует лента ручья (static), там поверхности нет. Заодно — границы
//                    уровня каждого тайла (группа потоков = тайл);
//   mainTilesReset — аргументы косвенного вызова: ни одного экземпляра;
//   mainTiles      — каждый кадр: тайлы с уровнем в frustum главного вида — в список экземпляров вызова
////////////////////////////////////////////////////////////////////////////////

#include "water_simulation.sh"

#define WATER_TILE 32

DM_SRV( Texture2D<float>, g_waterDepth, 3 );				// глубина воды симуляции, м
DM_SRV( StructuredBuffer<float2>, g_tileBoundsIn, 4 );		// mainTiles: наименьший и наибольший уровень тайла

DM_UAV( RWTexture2D<float>, g_level, 0 );					// уровень поверхности, м; noLevel — сухо
DM_UAV( RWStructuredBuffer<float2>, g_tileBounds, 1 );
DM_UAV( RWByteAddressBuffer, g_drawArgs, 2 );				// команда ExecuteIndirect (24 байта) и число команд
DM_UAV( RWStructuredBuffer<uint>, g_tileList, 3 );			// номера видимых тайлов — экземпляры вызова

// Раскладка — WaterSimulation::TilesParameters
cbuffer WaterTilesBuffer : register( b5 )
{
	float4 g_planes[6];			// frustum главного вида: нормали внутрь
	uint   g_indexCount;		// индексов сетки тайла
	uint   g_tilesPerSide;
	float  g_visibleDepth;		// мельче — воды не видно (только мокрая земля), м
	float  g_worldSize;
};

static const float noLevel = -1e30f;

groupshared uint gs_minLevel;
groupshared uint gs_maxLevel;

// float → uint с тем же порядком (для атомарных min / max)
uint orderedBits( float value )
{
	const uint bits = asuint( value );
	return ( bits & 0x80000000u ) ? ~bits : bits | 0x80000000u;
}

float fromOrderedBits( uint bits )
{
	return asfloat( ( bits & 0x80000000u ) ? bits & 0x7fffffffu : ~bits );
}

[numthreads( WATER_TILE, WATER_TILE, 1 )]
void mainSurface( uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint index : SV_GroupIndex )
{
	if( index == 0 )
	{
		gs_minLevel = 0xffffffffu;
		gs_maxLevel = 0u;
	}
	GroupMemoryBarrierWithGroupSync();

	const int2 cell = int2( id.xy );
	float level = noLevel;
	if( inside( cell ) )
	{
		const float depth = g_waterDepth[cell];
		if( depth >= g_visibleDepth )
			level = terrain( cell ) + depth;
		else if( depth >= 0.0f )	// < 0 — воду клетки рисует другое (лента ручья в режиме static)
		{
			float sum = 0.0f;
			float count = 0.0f;
			for( int y = -1; y <= 1; ++y )
			{
				for( int x = -1; x <= 1; ++x )
				{
					const int2 neighbor = cell + int2( x, y );
					if( inside( neighbor ) && g_waterDepth[neighbor] >= g_visibleDepth )
					{
						sum += terrain( neighbor ) + g_waterDepth[neighbor];
						count += 1.0f;
					}
				}
			}
			if( count > 0.0f )
				level = sum / count;
		}
		g_level[cell] = level;
	}
	if( level > noLevel )
	{
		InterlockedMin( gs_minLevel, orderedBits( level ) );
		InterlockedMax( gs_maxLevel, orderedBits( level ) );
	}
	GroupMemoryBarrierWithGroupSync();

	if( index == 0 )
	{
		const uint tile = group.y * g_tilesPerSide + group.x;
		g_tileBounds[tile] = gs_maxLevel == 0u ? float2( 1.0f, -1.0f ) : float2( fromOrderedBits( gs_minLevel ), fromOrderedBits( gs_maxLevel ) );
	}
}

[numthreads( 1, 1, 1 )]
void mainTilesReset()
{
	// {root-константа b9, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
	// StartInstanceLocation}, затем число команд — одна
	g_drawArgs.Store( 0, 0 );
	g_drawArgs.Store4( 4, uint4( g_indexCount, 0, 0, 0 ) );
	g_drawArgs.Store( 20, 0 );
	g_drawArgs.Store( 24, 1 );
}

[numthreads( 64, 1, 1 )]
void mainTiles( uint3 id : SV_DispatchThreadID )
{
	if( id.x >= g_tilesPerSide * g_tilesPerSide )
		return;
	const float2 bounds = g_tileBoundsIn[id.x];
	if( bounds.x > bounds.y )
		return;

	// Тайл — ячейки tile · WATER_TILE … + WATER_TILE (последний ряд — общий с соседом); ячейка (i, j) — точка
	// ((i + 0,5) · l, worldSize − (j + 0,5) · l): ось j текстуры идёт против Z мира
	const uint2 tile = uint2( id.x % g_tilesPerSide, id.x / g_tilesPerSide );
	const float2 first = ( tile * WATER_TILE + 0.5f ) * g_cellSize;
	const float2 last = first + WATER_TILE * g_cellSize;
	const float3 boxMin = float3( first.x, bounds.x, g_worldSize - last.y );
	const float3 boxMax = float3( last.x, bounds.y, g_worldSize - first.y );
	const float3 center = 0.5f * ( boxMin + boxMax );
	const float3 extent = 0.5f * ( boxMax - boxMin );
	[unroll] for( int i = 0; i < 6; ++i )
	{
		const float4 plane = g_planes[i];
		if( dot( plane.xyz, center ) + plane.w < -dot( abs( plane.xyz ), extent ) )
			return;
	}

	uint slot;
	g_drawArgs.InterlockedAdd( 8, 1, slot );
	g_tileList[slot] = id.x;
}
