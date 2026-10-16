////////////////////////////////////////////////////////////////////////////////
// Террейн CDLOD (Strugar, 2009): вершина патча — cdlod_patch.sh (общее с дальними горами), высота уровня L — мип L карты
// высот. Уровни мельче листа (−1…−6: квад 0,5 и 0,25 м, под дисплейсмент — до 1,56 см) — у камеры везде: высоту вершина
// берёт по своему положению из детальной плитки у русла или из карты высот (sampleFineHeight; мельче 0,25 м — билинейно);
// уровень −1 к концу диапазона перетекает в мип 0 карты высот. У камеры вершина ещё сдвигается вдоль нормали рельефа по
// высотам слоёв — дисплейсмент (terrain_layers.sh): так плиты и камни слоёв получают силуэт и тень.
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "cdlod.sh"
#include "terrain_layers.sh"
#include "bindless.sh"

float patchHeight( float2 worldXZ, float level )
{
	return sampleHeight( worldXZ, level );
}

float patchFineHeight( float2 worldXZ )
{
	return sampleFineHeight( worldXZ );
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

	float4 worldPosition = float4( vertex.position, 1.0f );
	const float3 basePosition = worldPosition.xyz;
	// Дисплейсмент у точки LOD (а не у камеры вида: в видах теней земля та же) — на любом уровне, сходит на нет к
	// displacementEnd; вдоль нормали самой детальной земли. Так же землю со смещением видят расстановка и частицы
	// (terrainSurfacePoint в terrain_height.sh)
	const float distanceLOD = lodDistance( basePosition );
	[branch] if( distanceLOD < terrainLayers().displacementEnd )
	{
		const float3 normal = fineNormal( basePosition.xz, 0.5f );
		worldPosition.xyz += normal * terrainDisplacement( basePosition, normal, distanceLOD );
	}

	PixelInputType output;
	output.position = mul( worldPosition, cb_viewProjectionMatrix );
	output.worldPosition = worldPosition.xyz;
	output.basePosition = basePosition;
	output.uv = heightMapUV( worldPosition.xz );
	output.lodDebug = float2( node.level, vertex.morphK );

	return output;
}
