////////////////////////////////////////////////////////////////////////////////
// Вершина патча CDLOD (Strugar, 2009) — общее у рельефа карты (cdlod.vs) и дальних гор вокруг неё (far_terrain.vs): узлы
// обоих — на одной сетке, вершина на их стыке считается одним кодом и даёт одно положение, поэтому трещин нет.
// Вершины общей сетки-патча размещаются по данным инстанса (четверть узла квадродерева), высота — по уровню LOD.
// К концу диапазона своего уровня нечётные вершины съезжают на сетку следующего, вдвое более грубого уровня, а высота
// перетекает в высоту следующего уровня, поэтому соседние уровни стыкуются без трещин и скачков.
// Высоту даёт тот, кто включает файл, — до включения:
//   float patchHeight( float2 worldXZ, float level )	— вершины уровня level ≥ 0 (мип карты по уровню)
//   float patchFineHeight( float2 worldXZ )			— самая детальная земля (уровни мельче листа)
// Вычисления — precise: у вершины стыка карты и гор шейдеры разные, а результат должен совпасть до бита
////////////////////////////////////////////////////////////////////////////////

#ifndef CDLOD_PATCH_SH
#define CDLOD_PATCH_SH

#include "slots.h"
#include "bindless.sh"
#include "terrain_layers.sh"

struct NodeInstance
{
	float2 origin;	// угол патча в мире (x, z)
	float  size;	// сторона патча
	float  level;	// уровень LOD, 0 — самый детальный, < 0 — мельче листа
};

DM_SRV( StructuredBuffer<NodeInstance>, g_instances, SLOT_INSTANCE_DATA );

struct PatchVertex
{
	float3 position;	// в мире, без дисплейсмента
	float  morphK;		// 0 — сетка своего уровня, 1 — следующего
};

// gridPos — вершина сетки-патча (0…g_gridDim по x и z)
PatchVertex placePatchVertex( NodeInstance node, float2 gridPos )
{
	precise float quadSize = node.size / g_gridDim;
	precise float2 worldXZ = node.origin + gridPos * quadSize;

	const bool detail = node.level < 0.0f;
	precise float approxHeight = detail ? patchFineHeight( worldXZ ) : patchHeight( worldXZ, node.level );
	const float4 morph = detail ? g_detailMorph[(uint)( -node.level ) - 1] : g_morphConsts[(uint)node.level];
	// От точки LOD вида, а не от его камеры: в видах теней геометрия та же, что у главного вида
	precise float morphK = saturate( ( lodDistance( float3( worldXZ.x, approxHeight, worldXZ.y ) ) - morph.x ) * morph.y );

	// У нечётных вершин frac( gridPos * 0.5 ) = 0.5: при morphK = 1 они совпадают с чётными соседями
	worldXZ -= frac( gridPos * 0.5f ) * 2.0f * quadSize * morphK;

	// При morphK = 1 высота — как у соседнего грубого уровня. Детальные: −2 → −1 — та же самая детальная земля, −1 → 0 —
	// уровень 0
	precise float height;
	if( detail )
		height = lerp( patchFineHeight( worldXZ ), node.level < -1.5f ? patchFineHeight( worldXZ ) : patchHeight( worldXZ, 0.0f ), morphK );
	else
		height = lerp( patchHeight( worldXZ, node.level ), patchHeight( worldXZ, node.level + 1.0f ), morphK );

	PatchVertex vertex;
	vertex.position = float3( worldXZ.x, height, worldXZ.y );
	vertex.morphK = morphK;
	return vertex;
}

#endif
