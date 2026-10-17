////////////////////////////////////////////////////////////////////////////////
// Константы расстановки (ScatterPass, Scatterer; Shaders/scatter.cs): слой, варианты, виды кадра. Общие для C++ и HLSL
// — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SCATTER_CONSTANTS_H
#define DM_SCATTER_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"
#include "view_limits.h"

// Пределы расстановки (ScatterPass): LOD и вариантов у слоя, секций у LOD, списков индексов — пар «вариант × LOD» и
// столько же списков перехода, групп секций списков — команд ExecuteIndirect на вид
#define SCATTER_MAX_LODS 4
#define SCATTER_MAX_VARIANTS 8
#define SCATTER_MAX_SECTIONS 4
#define SCATTER_MAX_LISTS ( SCATTER_MAX_VARIANTS * SCATTER_MAX_LODS * 2 )
#define SCATTER_MAX_GROUPS ( SCATTER_MAX_LISTS * SCATTER_MAX_SECTIONS )

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ScatterLayerBuffer, b4 )
{
	DM_FIELD( float, nearBorder );
	DM_FIELD( float, farBorder );
	DM_FIELD( float, nearFade );
	DM_FIELD( float, farFade );
	DM_FIELD( float, sizeMultiplier );
	DM_FIELD( float, cellSize );
	DM_FIELD( float, jitter );			// смещение внутри ячейки, доля шага
	DM_FIELD( float, alignToTerrain );	// 1 — ось Y инстанса по нормали террейна
	DM_FIELD( float3, rotationRange );		// предел случайного поворота вокруг осей X, Y, Z, радианы
	DM_FIELD( float, castShadow );		// 1 — слой отбрасывает тень солнца: инстансы попадают и в списки видов теней
	DM_FIELD( uint, variantCount );		// моделей слоя, 1…8
	DM_FIELD( uint, itemCapacity );		// ёмкость пула инстансов g_items
	DM_FIELD( uint, transitionCapacity );// ёмкость пула перехода g_transitions
	DM_FIELD( uint, indexStride );		// индексов на вид в g_indices (списки всех пар вида подряд)
	// Постоянный слой: кластеров по X и всего, ёмкость постоянного пула, запас границ кластера по XZ (наибольший радиус
	// экземпляра), м
	DM_FIELD( uint, clustersX );
	DM_FIELD( uint, clusterCount );
	DM_FIELD( uint, placedCapacity );
	DM_FIELD( float, clusterMargin );
	DM_FIELD( float, sizeVariation );		// разброс размера: случайный множитель от 1 − разброс до 1
	// Цвет земли у растения (scatter_ground.sh): доля у корня, высота перехода к своему цвету (м), доля по всему растению у
	// дальней границы кольца, разброс яркости по экземплярам (±)
	DM_FIELD( float, groundColor );
	DM_FIELD( float, groundColorHeight );
	DM_FIELD( float, groundColorFar );
	DM_FIELD( float, colorVariation );
	// Края кольца и прореживание: доля полосы исчезания, по которой разбросаны свои дальности экземпляров (0 — все исчезают
	// вместе); с какого расстояния прореживать, м, и доля убранных у дальней границы (0 — не прореживать)
	DM_FIELD( float, edgeSpread );
	DM_FIELD( float, thinStart );
	DM_FIELD( float, thinning );
	DM_FIELD( float, ditherFade );		// 1 — у краёв кольца экземпляр исчезает долей пикселей (дизеринг), а не размером
	// Куртины: размер пятен шума, м (0 — без куртин), и сила 0…1 — вес вида × e^(4 · сила · шум его группы)
	DM_FIELD( float, patchScale );
	DM_FIELD( float, patchStrength );
	// Фильтр по слоям земли: бит — слой материала террейна, на котором слой растёт, бит 31 — фильтр включён (0 — на любых);
	// порог — доля этих слоёв в весах земли, при которой плотность вдвое меньше (от удвоенной — полная)
	DM_FIELD( uint, groundLayers );
	DM_FIELD( float, groundThreshold );
	DM_FIELD( float, groundPreferences );	// 1 — у какого-то варианта предпочтение слоёв земли: вариант — по весам в точке
	DM_FIELD( uint, gridDim );			// кольцевой слой: ячеек сетки по стороне квадрата 2 · far вокруг камеры (main)
	DM_FIELD( float, layerPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( ScatterVariantsBuffer, b7 )
{
	// x — накопленная доля варианта (0…1), y — число LOD, z — 1: смена LOD дизерингом, w — с этого расстояния у видов
	// теней последний LOD (импостер; 0 — LOD как у главного вида)
	DM_ARRAY( float4, variants, SCATTER_MAX_VARIANTS );
	DM_ARRAY( float4, lodEnd, SCATTER_MAX_VARIANTS );	// дальности LOD 0…2 варианта, м: дальше — следующий LOD
	// Они же у видов теней (ScatterPass::setShadowLods): границы LOD модели × «Shadow LOD scale» модели и набора, детальнее
	// «Shadow min LOD» модели — 0; переход к импостеру — как в кадре (у теней его сдвигает w в g_variants)
	DM_ARRAY( float4, shadowLodEnd, SCATTER_MAX_VARIANTS );
	DM_ARRAY( float4, shadowCasting, SCATTER_MAX_VARIANTS );	// x — с этого LOD у видов теней экземпляра нет (импостер без тени); = числу LOD — у всех
	DM_ARRAY( float4, bounds, SCATTER_MAX_VARIANTS );	// сфера модели (LOD0) в её координатах: xyz — центр, w — радиус (при размере 1)
	DM_ARRAY( float4, footprints, SCATTER_MAX_VARIANTS );	// след основания (LOD0): xy — центр по X и Z, z — радиус по XZ (при размере 1)
	// Своя раскладка варианта: x — вес, y — × размер слоя, z — разброс размера (< 0 — слоя), w — наклон по земле (< 0 —
	// слоя, 0 / 1)
	DM_ARRAY( float4, placement, SCATTER_MAX_VARIANTS );
	// x — группа куртин: номер узора шума (у моделей одного вида — один); y — биты слоёв земли, которые вид предпочитает
	// (0 — нет), z — сила предпочтения (asfloat)
	DM_ARRAY( uint4, variantInfo, SCATTER_MAX_VARIANTS );
	DM_ARRAY( uint4, lists, SCATTER_MAX_LISTS );		// y — ёмкость списка индексов (на вид), z — число секций, w — начало списка в индексах вида
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

// Виды кадра: плоскости frustum каждого (нормированные, нормали внутрь) и параметры вида
#define DM_FIELD_PREFIX g_
DM_CONSTANTS( FrustumBuffer, b6 )
{
	DM_ARRAY( float4, frustumPlanes, DM_MAX_RENDER_VIEWS * 6 );
	// x — тип вида (DM_VIEW_*, view_limits.h: главный, каскад теней, дальний каскад, съёмка), y — запас; у каскада теней
	// z, w — полоса расстояний от камеры (RenderView::cascadeNear / cascadeFar): тень инстанса ложится не дальше её длины
	// от него
	DM_ARRAY( float4, viewParams, DM_MAX_RENDER_VIEWS );
	// У каскада теней: xy — полоса глубины взгляда, пиксели которой читают каскад (cascadeNear, cascadeDepthFar)
	DM_ARRAY( float4, viewDepths, DM_MAX_RENDER_VIEWS );
	DM_FIELD( float4, shadowCast );	// xyz — куда идёт свет источника теней, w — длина тени на метр высоты вдоль луча (0 — теней нет)
	DM_FIELD( uint, viewCount );
	DM_FIELD( uint3, frustumPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
