////////////////////////////////////////////////////////////////////////////////
// Поверхность воды: сетка тайла (GridMesh (WATER_TILE + 1)²) ставится на центры ячеек симуляции, высота — уровень
// воды (water_surface.cs, mainSurface). Вершина без уровня (сухо) — NaN: треугольники с ней отбрасываются, поэтому
// рисуется только вода и полоса в ячейку за урезом, где плоскость воды уходит под рельеф. Экземпляр — видимый тайл
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "water_surface.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float>, g_level, 0 );							// уровень поверхности, м; −1e30 — сухо
DM_SRV( StructuredBuffer<uint>, g_tiles, SLOT_INSTANCE_DATA );	// номера видимых тайлов

struct VertexInputType
{
	float3 position : POSITION;		// (i, 0, j) сетки тайла
	uint instanceId : SV_InstanceID;
};

static const float dryLevel = -1e29f;

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
