////////////////////////////////////////////////////////////////////////////////
// Поверхность воды: сетка тайла (GridMesh (WATER_TILE + 1)²) ставится на центры ячеек симуляции, высота — уровень
// воды (water_surface.cs, mainSurface). Вершина без уровня (сухо) — NaN: треугольники с ней отбрасываются, поэтому
// рисуется только вода и полоса в ячейку за урезом, где плоскость воды уходит под рельеф. Экземпляр — видимый тайл.
// WATER_DETAIL — детальная плитка у русел: сетка — вершина на тексель плитки детальной воды (0,25 м, с полем в тексель:
// плитка чуть шире своей площади, лишнее отсекает water.ps), высота — её уровень (water_detail.cs)
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "water_surface.sh"
#include "bindless.sh"
#ifdef WATER_DETAIL
#include "terrain_detail.sh"
DM_SRV( Texture2DArray<float4>, g_waterDetailTiles, SLOT_WATER_DETAIL );	// x — уровень поверхности, м; −1e30 — нет
#else
DM_SRV( Texture2D<float>, g_level, 0 );							// уровень поверхности, м; −1e30 — сухо
#endif
DM_SRV( StructuredBuffer<uint>, g_tiles, SLOT_INSTANCE_DATA );	// номера видимых тайлов

struct VertexInputType
{
	float3 position : POSITION;		// (i, 0, j) сетки тайла
	uint instanceId : SV_InstanceID;
};

static const float dryLevel = -1e29f;

#ifdef WATER_DETAIL

// Уровень текселя (i — по X, j — по Z мира) плитки slice; у сухого — fallback
float detailLevelAt( int2 texel, uint slice, uint side, float fallback )
{
	const float level = g_waterDetailTiles.Load( int4( clamp( texel, 0, (int)side - 1 ), slice, 0 ) ).x;
	return level > dryLevel ? level : fallback;
}

WaterPixelInput main( VertexInputType input )
{
	const uint tile = g_tiles[input.instanceId];
	const int2 detailTile = waterDetailTile( tile );
	const uint slice = g_terrainDetailIndex.Load( int3( detailTile, 0 ) ) - 1;
	uint side, sideZ, slices;
	g_waterDetailTiles.GetDimensions( side, sideZ, slices );
	// Строки сетки идут по +Z мира — как строки плитки: обход треугольников тот же, что у тайла 1 м
	const int2 texel = int2( input.position.x, input.position.z );

	WaterPixelInput output;
	output.tile = tile;
	const float level = detailLevelAt( texel, slice, side, dryLevel );
	if( level <= dryLevel )
	{
		output.position = asfloat( 0x7fc00000 );	// NaN — треугольник отбрасывается
		output.worldPosition = 0.0f;
		output.normal = float3( 0.0f, 1.0f, 0.0f );
		output.uv = 0.0f;
		return output;
	}

	// Тексель i — центр в начале плитки + ( i − 0,5 ) · шаг (поле в тексель с каждой стороны)
	const float tileSize = WATER_TILE * g_cellSize;
	const float step = tileSize / ( side - 2 );
	const float2 worldXZ = detailTile * tileSize + ( texel - 0.5f ) * step;
	const float3 worldPosition = float3( worldXZ.x, level, worldXZ.y );
	// Наклон поверхности по соседям; у сухого соседа — свой уровень
	const float left = detailLevelAt( texel + int2( -1, 0 ), slice, side, level );
	const float right = detailLevelAt( texel + int2( 1, 0 ), slice, side, level );
	const float back = detailLevelAt( texel + int2( 0, -1 ), slice, side, level );	// меньше Z
	const float front = detailLevelAt( texel + int2( 0, 1 ), slice, side, level );
	output.position = mul( float4( worldPosition, 1.0f ), cb_viewProjectionMatrix );
	output.worldPosition = worldPosition;
	output.normal = normalize( float3( left - right, 2.0f * step, back - front ) );
	output.uv = float2( worldXZ.x / g_worldSize, 1.0f - worldXZ.y / g_worldSize );
	return output;
}

#else

float levelAt( int2 cell, float fallback )
{
	if( any( cell < 0 ) || any( cell >= (int)g_size ) )
		return fallback;
	const float level = g_level.Load( int3( cell, 0 ) );
	return level > dryLevel ? level : fallback;
}

WaterPixelInput main( VertexInputType input )
{
	const uint tile = g_tiles[input.instanceId];
	const int2 tileCell = int2( tile % g_tilesPerSide, tile / g_tilesPerSide ) * WATER_TILE;
	// Ряды сетки идут по +Z мира, ряды текстуры — против: обход треугольников тот же, что у террейна
	const int2 cell = tileCell + int2( input.position.x, WATER_TILE - input.position.z );

	WaterPixelInput output;
	output.tile = tile;
	const float level = levelAt( cell, dryLevel );
	if( level <= dryLevel )
	{
		output.position = asfloat( 0x7fc00000 );	// NaN — треугольник отбрасывается
		output.worldPosition = 0.0f;
		output.normal = float3( 0.0f, 1.0f, 0.0f );
		output.uv = 0.0f;
		return output;
	}

	const float3 worldPosition = float3( ( cell.x + 0.5f ) * g_cellSize, level, g_worldSize - ( cell.y + 0.5f ) * g_cellSize );
	// Наклон поверхности по соседям; у сухого соседа — свой уровень
	const float left = levelAt( cell + int2( -1, 0 ), level );
	const float right = levelAt( cell + int2( 1, 0 ), level );
	const float back = levelAt( cell + int2( 0, 1 ), level );	// ряд текстуры ниже — меньше Z
	const float front = levelAt( cell + int2( 0, -1 ), level );
	output.position = mul( float4( worldPosition, 1.0f ), cb_viewProjectionMatrix );
	output.worldPosition = worldPosition;
	output.normal = normalize( float3( left - right, 2.0f * g_cellSize, back - front ) );
	output.uv = ( cell + 0.5f ) / g_size;
	return output;
}

#endif
