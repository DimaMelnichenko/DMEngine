////////////////////////////////////////////////////////////////////////////////
// Террейн CDLOD (Strugar, 2009): вершины общей сетки-патча размещаются по данным инстанса
// (четверть узла квадродерева), высота читается из мипа карты высот, равного уровню LOD. К концу
// диапазона своего уровня нечётные вершины съезжают на сетку следующего, вдвое более грубого уровня,
// а высота перетекает в мип следующего уровня, поэтому соседние уровни стыкуются без трещин и скачков.
// Уровни мельче листа (−1…−6: квад 0,5 и 0,25 м, под дисплейсмент — до 1,56 см) — у камеры везде: высоту вершина берёт по своему положению
// из детальной плитки у русла или из карты высот (sampleFineHeight; мельче 0,25 м — билинейно); уровень −1 к концу диапазона
// перетекает в мип 0 карты высот. У камеры вершина ещё сдвигается вдоль нормали рельефа по высотам слоёв — дисплейсмент
// (terrain_layers.sh): так плиты и камни слоёв получают силуэт и тень.
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "cdlod.sh"
#include "terrain_layers.sh"
#include "bindless.sh"

struct NodeInstance
{
	float2 origin;	// угол патча в мире (x, z)
	float  size;	// сторона патча
	float  level;	// уровень LOD, 0 — самый детальный
};

DM_SRV( StructuredBuffer<NodeInstance>, g_instances, SLOT_INSTANCE_DATA );

struct VertexInputType
{
	float3 position : POSITION;
	uint instanceId : SV_InstanceID;
};

PixelInputType main( VertexInputType input )
{
	NodeInstance node = g_instances[input.instanceId];

	float quadSize = node.size / g_gridDim;
	float2 gridPos = input.position.xz;
	float2 worldXZ = node.origin + gridPos * quadSize;

	const bool detail = node.level < 0.0f;
	float3 approxPosition = float3( worldXZ.x, detail ? sampleFineHeight( worldXZ ) : sampleHeight( worldXZ, node.level ), worldXZ.y );
	float4 morph = detail ? g_detailMorph[(uint)( -node.level ) - 1] : g_morphConsts[(uint)node.level];
	// От точки LOD вида, а не от его камеры: в видах теней геометрия та же, что у главного вида
	float morphK = saturate( ( lodDistance( approxPosition ) - morph.x ) * morph.y );

	// У нечётных вершин frac( gridPos * 0.5 ) = 0.5: при morphK = 1 они совпадают с чётными соседями
	worldXZ -= frac( gridPos * 0.5f ) * 2.0f * quadSize * morphK;

	// При morphK = 1 высота берётся из того же мипа, что у соседнего грубого уровня. Детальные: −2 → −1 — та же самая
	// детальная земля, −1 → 0 — мип 0 карты высот
	float height;
	if( detail )
		height = lerp( sampleFineHeight( worldXZ ), node.level < -1.5f ? sampleFineHeight( worldXZ ) : sampleHeight( worldXZ, 0.0f ), morphK );
	else
		height = lerp( sampleHeight( worldXZ, node.level ), sampleHeight( worldXZ, node.level + 1.0f ), morphK );
	float4 worldPosition = float4( worldXZ.x, height, worldXZ.y, 1.0f );
	const float3 basePosition = worldPosition.xyz;
	// Дисплейсмент у точки LOD (а не у камеры вида: в видах теней земля та же) — на любом уровне, сходит на нет к
	// displacementEnd; вдоль нормали самой детальной земли. Так же землю со смещением видят расстановка и частицы
	// (terrainSurfacePoint в terrain_height.sh)
	const float distanceLOD = lodDistance( basePosition );
	[branch] if( distanceLOD < terrainLayers().displacementEnd )
	{
		const float3 normal = fineNormal( worldXZ, 0.5f );
		worldPosition.xyz += normal * terrainDisplacement( basePosition, normal, distanceLOD );
	}

	PixelInputType output;
	output.position = mul( worldPosition, cb_viewProjectionMatrix );
	output.worldPosition = worldPosition.xyz;
	output.basePosition = basePosition;
	output.uv = heightMapUV( worldPosition.xz );
	output.lodDebug = float2( node.level, morphK );

	return output;
}
