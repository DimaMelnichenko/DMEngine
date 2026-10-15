////////////////////////////////////////////////////////////////////////////////
// Детальная вода у русел (класс WaterSimulation, docs/water.md, «Детальная вода у русел»): плитки — те же, что у
// детальной земли (TerrainHydrology::DetailTile, TerrainPipeline), 0,25 м с полем в тексель с каждой стороны, тексели по x и
// z мира (строка растёт с z).
//   mainDetailSurface — после расчёта течения: глубина над детальной землёй (уровень воды — ручья ближайшего отрезка или
//                       озера, что выше, его даёт конвейер), уровень поверхности там, где воды видно, у сухого текселя рядом
//                       с водой — наименьший уровень мокрых соседей (как mainSurface у сетки 1 м: плоскость воды уходит под
//                       землю, урез — их пересечение); течение — с сетки 1 м (билинейно), у мокрых ячеек его затем заменяет
//                       расчёт на детальной сетке (water_flow.cs, FLOW_DETAIL); границы уровней плиток — для отбора
// Группа потоков — 8 × 8 текселей плитки, z — плитка (срез)
////////////////////////////////////////////////////////////////////////////////

#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture2DArray<float4>, g_detailStatic, 0 );		// уровень воды (−1e9 — нет), u₀ по X и Z мира, R русла (DetailTile::water)
DM_SRV( Texture2DArray<float2>, g_terrainDetailTiles, 1 );	// детальная земля: нормированная высота, врез
DM_SRV( Texture2D<float4>, g_coarseWater, 2 );				// вода на сетке 1 м (SLOT_WATER): глубина, течение X, Z
DM_SRV( StructuredBuffer<uint2>, g_detailTileCoords, 3 );	// плитка среза — x, z по сетке плиток мира

DM_UAV( RWTexture2DArray<float4>, g_detailWater, 0 );		// уровень поверхности (−1e30 — нет), течение X, Z, глубина
DM_UAV( RWByteAddressBuffer, g_detailBounds, 1 );			// на плитку: ~наименьший и наибольший уровень (orderedBits)

// Раскладка — WaterSimulation::DetailParameters
cbuffer WaterDetailBuffer : register( b5 )
{
	float g_heightMultiplier;
	float g_heightOffset;
	float g_worldSize;
	float g_tileSize;			// сторона плитки, м
	float g_visibleDepth;		// мельче — воды не видно (мокрая земля), м
	uint  g_side;				// текселей плитки по стороне (с полем)
	float2 g_detailPadding;
};

static const float noLevel = -1e30f;

// float → uint с тем же порядком (для атомарных min / max)
uint orderedBits( float value )
{
	const uint bits = asuint( value );
	return ( bits & 0x80000000u ) ? ~bits : bits | 0x80000000u;
}

// Глубина воды над детальной землёй текселя, м (< 0 — суше) и уровень этой воды
float detailDepth( int2 texel, uint slice, out float level )
{
	const float bed = g_terrainDetailTiles.Load( int4( texel, slice, 0 ) ).x * g_heightMultiplier + g_heightOffset;
	const float4 water = g_detailStatic.Load( int4( texel, slice, 0 ) );
	level = water.x;
	return level > -1e8f ? level - bed : -1e30f;
}

[numthreads( 8, 8, 1 )]
void mainDetailSurface( uint3 id : SV_DispatchThreadID )
{
	const int2 texel = int2( id.xy );
	const uint slice = id.z;
	if( any( texel >= (int)g_side ) )
		return;
	float waterLevel;
	const float depth = detailDepth( texel, slice, waterLevel );
	float level = noLevel;
	if( depth >= g_visibleDepth )
		level = waterLevel;
	else
	{
		// Наименьший уровень мокрых соседей: у озера все равны, у ручья на склоне — сосед ниже по течению
		float lowest = 1e30f;
		[unroll] for( int y = -1; y <= 1; ++y )
		{
			[unroll] for( int x = -1; x <= 1; ++x )
			{
				const int2 neighbor = clamp( texel + int2( x, y ), 0, (int)g_side - 1 );
				float neighborLevel;
				if( detailDepth( neighbor, slice, neighborLevel ) >= g_visibleDepth )
					lowest = min( lowest, neighborLevel );
			}
		}
		if( lowest < 1e29f )
			level = lowest;
	}

	// Течение — решение на сетке 1 м (озёра, струи, камни), билинейно в точке текселя: тексель i — центр в начале плитки
	// + ( i − 0,5 ) · шаг; UV карты высот — строки против Z мира
	const float step = g_tileSize / ( g_side - 2 );
	const float2 worldXZ = g_detailTileCoords[slice] * g_tileSize + ( texel - 0.5f ) * step;
	const float2 uv = float2( worldXZ.x / g_worldSize, 1.0f - worldXZ.y / g_worldSize );
	const float2 velocity = g_coarseWater.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).yz;
	g_detailWater[uint3( texel, slice )] = float4( level, velocity, max( depth, 0.0f ) );

	if( level > noLevel )
	{
		const uint bits = orderedBits( level );
		uint previous;
		g_detailBounds.InterlockedMax( slice * 8, ~bits, previous );
		g_detailBounds.InterlockedMax( slice * 8 + 4, bits, previous );
	}
}
