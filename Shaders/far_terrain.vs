////////////////////////////////////////////////////////////////////////////////
// Дальние горы вокруг карты (FarTerrain): вершина патча — cdlod_patch.sh, как у рельефа карты, высота — far_terrain.sh (у края
// карты — её высота тем же кодом). Без дисплейсмента
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "cdlod.sh"
#include "terrain_layers.sh"
#include "far_terrain.sh"

float patchHeight( float2 worldXZ, float level )
{
	return farHeight( worldXZ, level );
}

float patchFineHeight( float2 worldXZ )
{
	return farFineHeight( worldXZ );
}

#include "cdlod_patch.sh"

struct VertexInputType
{
	float3 position : POSITION;
	uint instanceId : SV_InstanceID;
};

PixelInputType main( VertexInputType input )
{
	const NodeInstance node = g_instances[input.instanceId];
	const PatchVertex vertex = placePatchVertex( node, input.position.xz );

	PixelInputType output;
	output.position = mul( float4( vertex.position, 1.0f ), cb_viewProjectionMatrix );
	output.worldPosition = vertex.position;
	output.basePosition = vertex.position;
	output.uv = heightMapUV( vertex.position.xz );
	output.lodDebug = float2( node.level, vertex.morphK );
	return output;
}
