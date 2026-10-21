////////////////////////////////////////////////////////////////////////////////
// Поверхность воды: сетка тайла (GridMesh (WATER_TILE + 1)²) ставится на центры ячеек симуляции, высота — уровень
// воды (water_surface.cs, mainSurface). Вершина без уровня (сухо) — NaN: треугольники с ней отбрасываются, поэтому
// рисуется только вода и полоса в ячейку за урезом, где плоскость воды уходит под рельеф. Экземпляр — видимый тайл.
// WATER_DETAIL — детальная плитка у русел: сетка — вершина на тексель плитки детальной воды (0,25 м, с полем в тексель:
// плитка чуть шире своей площади, лишнее отсекает water.ps), высота — её уровень (water_detail.cs).
// WATER_NEAR — окно подробного течения у камеры (NearWaterFlow): сетка — вершина на ячейку окна (6,25 см), один экземпляр,
// высота — уровень детальной воды билинейно и подъём по давлению течения окна (к краю окна — к нулю: там поверхность та же,
// что у детальных плиток)
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "water_surface.sh"
#include "bindless.sh"
#if defined( WATER_DETAIL ) || defined( WATER_NEAR )
#include "terrain_detail.sh"
DM_SRV( Texture2DArray<float4>, g_waterDetailTiles, SLOT_WATER_DETAIL );	// x — уровень поверхности, м; −1e30 — нет
#ifdef WATER_NEAR
#include "water_near.sh"
#include "water_standing.sh"
#endif
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

// Вершина без воды: NaN — треугольники с ней отбрасываются
WaterPixelInput dryVertex()
{
	WaterPixelInput output;
	output.position = asfloat( 0x7fc00000 );
	output.worldPosition = 0.0f;
	output.normal = float3( 0.0f, 1.0f, 0.0f );
	output.uv = 0.0f;
	output.tile = 0;
	return output;
}

#if defined( WATER_NEAR )

// Уровень детальной воды в точке мира — билинейно по четырём текселям её плитки (вершина окна — между ними); нет плитки,
// её не рисует детальная сетка (дальше g_detailDistance — тайлы 1 м) или среди четырёх сухой тексель — dryLevel: окно
// рисует ту же воду, что детальные плитки
// Детальная плитка под точкой мира рисует воду (есть и ближе g_detailDistance): её место и номер + 1
bool detailTileRendered( float2 worldXZ, out int2 tile, out uint index )
{
	const float tileSize = WATER_TILE * g_cellSize;
	tile = int2( floor( worldXZ / tileSize ) );
	index = 0;
	uint tilesX, tilesZ;
	g_terrainDetailIndex.GetDimensions( tilesX, tilesZ );
	if( any( tile < 0 ) || tile.x >= (int)tilesX || tile.y >= (int)tilesZ )
		return false;
	index = g_terrainDetailIndex.Load( int3( tile, 0 ) );
	return waterDetailRendered( tile, index, cb_cameraPosition );
}

float detailLevel( float2 worldXZ )
{
	const float tileSize = WATER_TILE * g_cellSize;
	int2 tile;
	uint index;
	if( !detailTileRendered( worldXZ, tile, index ) )
		return dryLevel;
	uint side, sideZ, slices;
	g_waterDetailTiles.GetDimensions( side, sideZ, slices );
	// Тексель i — центр в начале плитки + ( i − 0,5 ) · шаг (поле в тексель с каждой стороны)
	const float2 position = ( worldXZ - tile * tileSize ) / ( tileSize / ( side - 2 ) ) + 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	float level = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const float value = g_waterDetailTiles.Load( int4( clamp( base + corner, 0, (int)side - 1 ), index - 1, 0 ) ).x;
		if( value <= dryLevel )
			return dryLevel;
		level += value * ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
	}
	return level;
}

// Уровень поверхности в центре ячейки окна: детальная вода, подъём по давлению (к краю окна — к нулю) и длинные гребни
// стоячих волн; сухо — fallback
float nearLevel( int2 cell, float fallback )
{
	const float2 worldXZ = ( cell + 0.5f ) * cb_waterNear.z;
	const float level = detailLevel( worldXZ );
	return level > dryLevel ? level + loadNearWater( cell ).x * nearWaterWeight( worldXZ ) + standingCrestHeight( worldXZ ) : fallback;
}

WaterPixelInput main( VertexInputType input )
{
	// Вершина (i, j) — ячейка окна от его угла (j — по Z мира); окна нет (константы кадра — с прошлого расчёта окна) или
	// сетка больше окна (его сторона сменилась) — сухо
	const int2 index = int2( input.position.x, input.position.z );
	const int size = (int)cb_waterNear.w;
	if( size <= 0 || any( index >= size ) )
		return dryVertex();
	const float step = cb_waterNear.z;
	const int2 cell = int2( round( cb_waterNear.xy / step ) ) + index;
	float level = nearLevel( cell, dryLevel );
	// Вершина за краем детальных плиток (там вода — тайлы 1 м, пиксели за краем отбрасывает clipWaterSurface) — уровень
	// соседней вершины по эту сторону: иначе квадрат через край пропадал, и между сеткой окна (вершины — в центрах ячеек) и
	// тайлами 1 м (от самого края) оставалась щель в полъячейки — сквозь воду виден рыжий шов дна (Dimok, 2026-10-21)
	int2 tile;
	uint tileIndex;
	if( level <= dryLevel && !detailTileRendered( ( cell + 0.5f ) * step, tile, tileIndex ) )
		[unroll] for( uint k = 0; k < 4; ++k )
			if( level <= dryLevel )
				level = nearLevel( cell + ( k < 2 ? int2( k == 0 ? 1 : -1, 0 ) : int2( 0, k == 2 ? 1 : -1 ) ), dryLevel );
	if( level <= dryLevel )
		return dryVertex();

	const float2 worldXZ = ( cell + 0.5f ) * step;
	const float3 worldPosition = float3( worldXZ.x, level, worldXZ.y );
	// Наклон поверхности по соседним ячейкам; у сухого соседа — свой уровень
	const float left = nearLevel( cell + int2( -1, 0 ), level );
	const float right = nearLevel( cell + int2( 1, 0 ), level );
	const float back = nearLevel( cell + int2( 0, -1 ), level );	// меньше Z
	const float front = nearLevel( cell + int2( 0, 1 ), level );
	WaterPixelInput output;
	output.tile = 0;
	output.position = mul( float4( worldPosition, 1.0f ), cb_viewProjectionMatrix );
	output.worldPosition = worldPosition;
	output.normal = normalize( float3( left - right, 2.0f * step, back - front ) );
	output.uv = float2( worldXZ.x / g_worldSize, 1.0f - worldXZ.y / g_worldSize );
	return output;
}

#elif defined( WATER_DETAIL )

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
