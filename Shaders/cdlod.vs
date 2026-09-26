////////////////////////////////////////////////////////////////////////////////
// Террейн CDLOD (Strugar, 2009): вершины общей сетки-патча размещаются по данным инстанса
// (четверть узла квадродерева), высота читается из мипа карты высот, равного уровню LOD. К концу
// диапазона своего уровня нечётные вершины съезжают на сетку следующего, вдвое более грубого уровня,
// а высота перетекает в мип следующего уровня, поэтому соседние уровни стыкуются без трещин и скачков.
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "cdlod.sh"

struct NodeInstance
{
	float2 origin;	// угол патча в мире (x, z)
	float  size;	// сторона патча
	float  level;	// уровень LOD, 0 — самый детальный
};

StructuredBuffer<NodeInstance> g_instances : register( t1 );

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

	float3 approxPosition = float3( worldXZ.x, sampleHeight( worldXZ, node.level ), worldXZ.y );
	float4 morph = g_morphConsts[(uint)node.level];
	// От точки LOD вида, а не от его камеры: в видах теней геометрия та же, что у главного вида
	float morphK = saturate( ( distance( approxPosition, cb_lodOrigin ) - morph.x ) * morph.y );

	// У нечётных вершин frac( gridPos * 0.5 ) = 0.5: при morphK = 1 они совпадают с чётными соседями
	worldXZ -= frac( gridPos * 0.5f ) * 2.0f * quadSize * morphK;

	// При morphK = 1 высота берётся из того же мипа, что у соседнего грубого уровня
	float height = lerp( sampleHeight( worldXZ, node.level ), sampleHeight( worldXZ, node.level + 1.0f ), morphK );
	float4 worldPosition = float4( worldXZ.x, height, worldXZ.y, 1.0f );

	PixelInputType output;
	output.position = mul( worldPosition, cb_viewProjectionMatrix );
	output.worldPosition = worldPosition.xyz;
	output.uv = heightMapUV( worldPosition.xz );
	output.lodDebug = float2( node.level, morphK );

	return output;
}
