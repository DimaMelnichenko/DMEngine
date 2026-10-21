////////////////////////////////////////////////////////////////////////////////
// Расстановка слоя набора (Scatterer) — растения: трава, цветы, камешки. Инстансы раскладываются по сетке с шагом g_cellSize,
// привязанной к миру, в кольце g_nearBorder…g_farBorder вокруг камеры. Смещение в ячейке, размер и поворот — хеш
// координат ячейки, поэтому при движении камеры инстансы остаются на своих местах. Маска слоя задаёт вероятность
// появления и размер, у краёв кольца размер плавно уходит в ноль. Модель инстанса — один из вариантов слоя по весам
// (как Mesh Entries у Static Mesh Spawner в PCG UE), по случайному числу ячейки: варианты делят сетку и не пересекаются.
//
// Инстанс раскладывается один раз за кадр и попадает в общий пул (g_items); у каждого вида кадра (главная камера и
// каскады теней — FrustumBuffer) свои списки индексов «вариант × LOD» (g_indices): в них инстанс входит, только если
// виден в этом виде, а LOD у вида свой — по расстоянию до точки LOD и множителю вида (у каскадов теней LOD может быть
// грубее). Позиция, поворот и размер от LOD не зависят, поэтому при смене LOD у растения меняется только меш. Дальность
// LOD у каждого экземпляра своя — дальности модели со сдвигом по случайному числу ячейки и шуму мира
// (Shaders/lod_transition.h), поэтому граница LOD не идёт дугой; у варианта со сменой LOD дизерингом экземпляр в полосе
// перехода попадает в списки перехода обоих LOD с долей перехода (Dithered LOD Transition в UE, Shaders/lod_dither.sh) —
// в пул перехода g_transitions.
//
// Деревья на дальних горах (ScatterLayers.placement far_forest, placeFarTree): кольцевой слой за краем карты — высота и
// плотность леса — гор (FarTerrain, far_ground.sh), у края карты — её маска леса, продолженная наружу.
//
// Постоянный слой (лес, ScatterLayers.persistent): экземпляры раскладываются один раз на всю карту (placeWorld) в
// постоянный пул g_placed кластерами — группа потоков на квадрат PLACED_CLUSTER_CELLS² ячеек, экземпляры кластера подряд,
// у кластера — начало, число и диапазон высот (как кластеры HISM в UE). Каждый кадр cullPlaced отсекает кластер по видам
// кадра целиком, а экземпляры видимых кластеров — так же, как кольцевой слой (emitInstance): в пул кадра и списки видов.
//
// После раскладки buildCommands собирает команды ExecuteIndirect: по виду и группе (секции с одним материалом и
// состоянием — ScatterPass::groups) подряд команды {начало списка индексов (root-константа b9), DrawIndexedInstanced},
// число команд группы — счётчик в g_counters. Один ExecuteIndirect на группу на вид вместо вызова на каждую секцию
// каждого списка; счётчики списков — там же.
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#include "samplers.sh"
#include "common.vs"
#include "terrain_height.sh"
#include "scatter_ground.sh"
#include "water.sh"
#include "lod_transition.h"
#include "bindless.sh"
#include "scatter_constants.h"
#include "canopy.sh"
#include "far_ground.sh"

// Пределы — scatter_constants.h; номер списка — как ScatterPass::listIndex
static const uint maxLods = SCATTER_MAX_LODS;
static const uint maxVariants = SCATTER_MAX_VARIANTS;
#define MAX_SECTIONS SCATTER_MAX_SECTIONS
#define MAX_LISTS SCATTER_MAX_LISTS	// обычные списки пар «вариант × LOD», затем списки перехода
#define MAX_VIEWS DM_MAX_RENDER_VIEWS	// maxRenderViews: главный вид, каскады теней, дальний каскад и съёмка
#define MAX_GROUPS SCATTER_MAX_GROUPS


uint listIndex( uint variant, uint lod, bool transition )
{
	return ( transition ? maxVariants * maxLods : 0 ) + variant * maxLods + lod;
}

// Счётчики (g_counters, байты): пулы, списки индексов по видам, команды групп по видам (ScatterPass::*CountOffset)
#define COUNTER_ITEMS 0
#define COUNTER_TRANSITIONS 4
uint listCountOffset( uint view, uint list )
{
	return 8 + ( view * MAX_LISTS + list ) * 4;
}
uint groupCountOffset( uint view, uint group )
{
	return 8 + MAX_VIEWS * MAX_LISTS * 4 + ( view * MAX_GROUPS + group ) * 4;
}
#define COMMAND_STRIDE 24	// root-константа (начало списка индексов) + D3D12_DRAW_INDEXED_ARGUMENTS

// Совпадает с InstanceParam в instance.sh при INST_POS, INST_SCALE и INST_ROTATE
struct ScatterItem
{
	float3 position;
	float  size;
	float4 rotation;	// кватернион
	uint4  ground;		// цвет земли под экземпляром (scatter_ground.sh); нули — свой цвет
};

// Экземпляр списка перехода — ScatterPass::ScatterTransitionItem, InstanceParam с LOD_DITHER в instance.sh
struct ScatterTransitionItem
{
	ScatterItem item;
	float  lodDither;	// (0; 1) — уходящий LOD, (−1; 0) — приходящий (Shaders/lod_dither.sh)
	float3 padding;
};

DM_UAV( RWByteAddressBuffer, g_counters, 0 );
DM_UAV( RWStructuredBuffer<ScatterItem>, g_items, 1 );					// пул инстансов
DM_UAV( RWStructuredBuffer<ScatterTransitionItem>, g_transitions, 2 );	// пул перехода
DM_UAV( RWStructuredBuffer<uint>, g_indices, 3 );						// списки индексов по видам: индексы в пуле списка
DM_UAV( RWByteAddressBuffer, g_commands, 4 );							// команды ExecuteIndirect по видам и группам
DM_SRV( Texture2D, g_densityMask, 2 );
DM_SRV( Texture2D, g_windExposure, 8 );	// открытость ветру (mask_wind_exposure, UV карты высот) — флаговые деревья

// Постоянный слой: экземпляр — ScatterPass::PlacedItem, кластер — x начало и y число экземпляров в g_placed, z и w —
// наименьшая и наибольшая высота (orderedFloat)
#define PLACED_CLUSTER_CELLS 8	// ScatterPass::clusterCells: ячеек по стороне кластера, группа потоков placeWorld
struct PlacedItem
{
	ScatterItem item;	// размер — без исчезания у краёв кольца
	uint   variant;
	float  lodScale;	// своя доля дальностей LOD экземпляра
	float2 padding;
};
DM_UAV( RWStructuredBuffer<PlacedItem>, g_placed, 5 );
DM_UAV( RWStructuredBuffer<uint4>, g_clusters, 6 );
DM_UAV( RWByteAddressBuffer, g_placedCounter, 7 );
DM_SRV( StructuredBuffer<PlacedItem>, g_placedItems, 5 );
DM_SRV( StructuredBuffer<uint4>, g_clusterRanges, 6 );
// Секция списка: x — число индексов меша, y — начало индексов, z — начало вершин, w — группа + 1 (0 — секции нет)
DM_SRV( StructuredBuffer<uint4>, g_sectionArgs, 3 );
// Группа: x — начало команд группы в командах вида, y — ёмкость (число секций списков в группе)
DM_SRV( StructuredBuffer<uint4>, g_groups, 4 );

uint hash( uint x )
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

// Случайное число 0…1, одно и то же для ячейки и номера величины
float random( int2 cell, uint index )
{
	uint h = hash( asuint( cell.x ) ^ hash( asuint( cell.y ) ^ hash( index ) ) );
	return ( h & 0x00ffffffU ) / 16777216.0f;
}

// Низкочастотный шум мира −1…1 (value noise): случайные значения в узлах сетки с шагом size метров, между ними —
// гладкая интерполяция
float worldNoise( float2 position, float size, uint index )
{
	const float2 p = position / size;
	const int2 node = (int2)floor( p );
	float2 f = p - (float2)node;
	f = f * f * ( 3.0f - 2.0f * f );
	const float bottom = lerp( random( node, index ), random( node + int2( 1, 0 ), index ), f.x );
	const float top = lerp( random( node + int2( 0, 1 ), index ), random( node + int2( 1, 1 ), index ), f.x );
	return lerp( bottom, top, f.y ) * 2.0f - 1.0f;
}

// Куртины: шум группы −1…1 — пятна размером g_patchScale и их неровный край (второй слой втрое мельче); узор — свой у
// группы и слоя (seed)
float patchNoise( float2 worldXZ, uint group, uint seed )
{
	const uint index = seed + 32 + group * 2;
	return 0.7f * worldNoise( worldXZ, g_patchScale, index ) + 0.3f * worldNoise( worldXZ, g_patchScale * 0.35f, index + 1 );
}

// Вариант ячейки по весам в точке: с куртинами — вес × e^(4 · сила · шум его группы), с предпочтением слоёв земли —
// × (1 − сила + сила × доля слоёв вида в весах земли). Порог — случайное число ячейки pick (то же, что без них): пятна и
// слои на месте мира, виды не «пересаживаются» при движении камеры
uint pickWeightedVariant( float2 worldXZ, float pick, uint seed )
{
	const bool patches = g_patchScale > 0.0f && g_patchStrength > 0.0f;
	float ground[maxLayers];
	[unroll] for( uint layer = 0; layer < maxLayers; ++layer )
		ground[layer] = 0.0f;
	[branch] if( g_groundPreferences > 0.5f )
		terrainLayerWeightsAll( worldXZ, ground );
	float weights[maxVariants];
	float total = 0.0f;
	[loop] for( uint v = 0; v < g_variantCount; ++v )
	{
		float weight = g_placement[v].x;
		[branch] if( weight > 0.0f && patches )
			weight *= exp( 4.0f * g_patchStrength * patchNoise( worldXZ, g_variantInfo[v].x, seed ) );
		[branch] if( weight > 0.0f && g_variantInfo[v].y != 0 )
			weight *= lerp( 1.0f, layerMaskShare( ground, g_variantInfo[v].y ), asfloat( g_variantInfo[v].z ) );
		weights[v] = weight;
		total += weight;
	}
	float threshold = pick * total;
	uint variant = 0;
	[loop] for( ; variant + 1 < g_variantCount; ++variant )
	{
		threshold -= weights[variant];
		if( threshold < 0.0f )
			break;
	}
	return variant;
}

// Место по счётчику с ёмкостью: полон — счётчик возвращается, чтобы отрисовка не читала за концом
bool reserve( uint countOffset, uint capacity, out uint index )
{
	g_counters.InterlockedAdd( countOffset, 1, index );
	if( index < capacity )
		return true;
	g_counters.InterlockedAdd( countOffset, 0xffffffffU );
	return false;
}

// Индекс пула — в список вида
void pushIndex( uint view, uint list, uint poolIndex )
{
	uint slot;
	if( reserve( listCountOffset( view, list ), g_lists[list].y, slot ) )
		g_indices[view * g_indexStride + g_lists[list].w + slot] = poolIndex;
}

float4 quaternionAxisAngle( float3 axis, float angle )
{
	float s, c;
	sincos( angle * 0.5f, s, c );
	return float4( axis * s, c );
}

// Поворот b, затем a
float4 quaternionMul( float4 a, float4 b )
{
	return float4( a.w * b.xyz + b.w * a.xyz + cross( a.xyz, b.xyz ), a.w * b.w - dot( a.xyz, b.xyz ) );
}

// Поворот единичного вектора from в to (to не противоположен from)
float4 quaternionFromTo( float3 from, float3 to )
{
	return normalize( float4( cross( from, to ), 1.0f + dot( from, to ) ) );
}

// Поворот вектора кватернионом (как rotateByQuaternion в instance.sh)
float3 quaternionRotate( float3 v, float4 q )
{
	return v + 2.0f * cross( q.xyz, cross( q.xyz, v ) + q.w * v );
}

// Сфера, охватывающая обе
void enclose( float3 c1, float r1, float3 c2, float r2, out float3 center, out float radius )
{
	const float d = distance( c1, c2 );
	if( d + r2 <= r1 ) { center = c1; radius = r1; return; }
	if( d + r1 <= r2 ) { center = c2; radius = r2; return; }
	radius = ( d + r1 + r2 ) * 0.5f;
	center = c1 + ( c2 - c1 ) * ( ( radius - r1 ) / max( d, 1e-6f ) );
}

// float → uint с тем же порядком (атомарные min / max высот кластера) и обратно
uint orderedFloat( float f )
{
	const uint u = asuint( f );
	return ( u & 0x80000000U ) ? ~u : ( u | 0x80000000U );
}
float unorderedFloat( uint u )
{
	return asfloat( ( u & 0x80000000U ) ? ( u & 0x7fffffffU ) : ~u );
}

bool insideFrustum( uint view, float3 center, float radius )
{
	[unroll]
	for( uint i = 0; i < 6; ++i )
	{
		const float4 plane = g_frustumPlanes[view * 6 + i];
		if( dot( plane.xyz, center ) + plane.w < -radius )
			return false;
	}
	return true;
}

// Тип вида кадра (DM_VIEW_*): каскады теней и дальний каскад — виды теней (свои LOD, только отбрасывающие тень слои), съёмка
// сцены с другой камеры — как главный, но со своей пирамидой видимости
bool shadowView( uint view )
{
	const uint kind = (uint)g_viewParams[view].x;
	return kind == DM_VIEW_SHADOW || kind == DM_VIEW_FAR_SHADOW;
}

bool captureView( uint view )
{
	return (uint)g_viewParams[view].x == DM_VIEW_CAPTURE;
}

// Виды съёмки кадра — маской: кольцевые слои (трава, камешки) в съёмку не идут — их кольцо вокруг главной камеры
uint captureViews()
{
	uint mask = 0;
	[loop] for( uint view = 1; view < g_viewCount; ++view )
		mask |= captureView( view ) ? 1u << view : 0;
	return mask;
}

// LOD экземпляра в виде: первый, чья дальность не меньше расстояния; последний — до конца кольца. Со сменой LOD
// дизерингом в полосе дальность × (1 ± LOD_TRANSITION_WIDTH / 2) экземпляр переходит к следующему LOD, transition — доля
void selectLod( float distanceToLodOrigin, float4 lodEnd, uint lodCount, bool dithered, out uint lod, out float transition )
{
	lod = 0;
	transition = 0.0f;
	[unroll] for( uint i = 0; i < 3; ++i )
	{
		const float halfBand = dithered ? lodEnd[i] * ( LOD_TRANSITION_WIDTH * 0.5f ) : 0.0f;
		if( lod == i && i + 1 < lodCount && distanceToLodOrigin > lodEnd[i] - halfBand )
		{
			if( distanceToLodOrigin >= lodEnd[i] + halfBand )
				lod = i + 1;
			else
				transition = ( distanceToLodOrigin - ( lodEnd[i] - halfBand ) ) / ( 2.0f * halfBand );
		}
	}
}

// Доля растительности, которую убирают в точке правки рельефа, русло и вода: 0 — растёт, 1 — убрана.
// Маска слоя без того, что убрали правки рельефа, и без воды симуляции (SLOT_WATER: её привязывает WaterSimulation,
// объект сцены раньше расстановки; без воды у уровня — нули). Кольцевые слои следуют за водой каждый кадр, постоянные
// (лес) — по воде первого кадра.
// В детальной плитке у русла — ещё и по врезу (0,25 м). Вода — по глубине в точке (уровень воды против высоты точки,
// waterDepthAt): урез внутри клетки, узкий ручей и мелкий слив из озера не выкашивают метровую полосу и не зарастают
float foliageClear( float2 worldXZ )
{
	const float2 uv = terrainUV( worldXZ );
	const float channelClear = terrainChannelClear( worldXZ );
	const float waterClear = waterFoliageClear( uv, terrainHeight( worldXZ ) );
	return 1.0f - ( 1.0f - terrainFoliageClear( uv ) ) * ( 1.0f - max( channelClear, 0.0f ) ) * ( 1.0f - waterClear );
}

// Фильтр по слоям земли (как Landscape Grass Type в UE): множитель плотности по доле слоёв g_groundLayers в весах земли
// — splat-карта, у русел и мазков детальный слой весов. Плавный порог, а не доля: маска правил травы и так — вес травяных
// слоёв, и плотность «маска × доля» на краю луга была бы её квадратом; мазок снега или гальки по лугу гасит траву
float groundFilter( float2 worldXZ )
{
	if( g_groundLayers == 0 )
		return 1.0f;
	const float share = terrainLayerShare( worldXZ, g_groundLayers & ( ( 1u << maxLayers ) - 1u ) );
	return g_groundThreshold > 0.0f ? smoothstep( 0.0f, 2.0f * g_groundThreshold, share ) : ( share > minWeight ? 1.0f : 0.0f );
}

// След шире детальной плитки у русла (0,25 м) проверяется по краю: центр следа и четыре точки на его окружности
static const float footprintCheckRadius = 0.25f;

// Высота земли под точкой: на смещённой земле (displaced) — со смещением дисплейсмента у камеры (terrainSurfacePoint), иначе
// — карта высот
float groundHeight( float2 worldXZ, bool displaced )
{
	return displaced ? terrainSurfacePoint( worldXZ ).y : terrainHeight( worldXZ );
}

// Экземпляр в ячейке сетки мира: смещение, плотность маски (вероятность и размер), высота, размер без исчезания,
// поворот, вариант по весам и своя доля дальностей LOD. Все случайные величины — хеш ячейки и номера величины, поэтому у
// ячейки всегда один и тот же экземпляр — и у кольцевого слоя каждый кадр, и у постоянного при раскладке. displaced — на
// земле со смещением дисплейсмента (кольцевой слой: трава и камешки не тонут в поднятых камнях слоёв и не висят над щелями;
// постоянный — без него: раскладка раз, а смещение зависит от камеры); посадка по следу — по той же земле
bool placeInCell( int2 cell, uint seed, bool displaced, out ScatterItem item, out uint variant, out float lodScale )
{
	item = (ScatterItem)0;
	variant = 0;
	lodScale = 1.0f;

	float2 offset = ( float2( random( cell, seed ), random( cell, seed + 1 ) ) - 0.5f ) * g_jitter;
	float2 worldXZ = ( (float2)cell + 0.5f + offset ) * g_cellSize;

	float2 uv = terrainUV( worldXZ );
	if( any( uv < 0.0f ) || any( uv > 1.0f ) )
		return false;

	// Под кронами деревьев — реже (карта полога; лесу и тем, кому всё равно, «Under canopy» = 1 — без выборки), пятнами: в
	// тени трава растёт там, куда пробивается свет, а не ровной редкой сеткой — та в низком ракурсе читается рядами
	float canopy = 1.0f;
	[branch] if( g_underCanopy < 1.0f )
	{
		const float cover = canopyCover( uv );
		float under = g_underCanopy;
		[branch] if( cover > 0.0f && g_canopyPatchStrength > 0.0f && g_canopyPatchScale > 0.0f )
		{
			const float noise = 0.7f * worldNoise( worldXZ, g_canopyPatchScale, seed + 96 ) +
								0.3f * worldNoise( worldXZ, g_canopyPatchScale * 0.35f, seed + 97 );
			under = min( under * max( 1.0f + 2.0f * g_canopyPatchStrength * noise, 0.0f ), 1.0f );
		}
		canopy = lerp( 1.0f, under, cover );
	}
	const float density = g_densityMask.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).r * ( 1.0f - foliageClear( worldXZ ) ) *
						  groundFilter( worldXZ ) * canopy;
	const float chance = random( cell, seed + 2 );
	if( chance >= density )
		return false;

	// Вариант — по накопленным долям весов; отдельное случайное число ячейки (прежние 0…6 от вариантов не зависят). С
	// куртинами и предпочтением слоёв земли — по весам в точке (pickWeightedVariant)
	const float pick = random( cell, seed + 7 );
	if( ( g_patchScale > 0.0f && g_patchStrength > 0.0f ) || g_groundPreferences > 0.5f )
		variant = pickWeightedVariant( worldXZ, pick, seed );
	else
		[loop] for( uint v = 0; v + 1 < g_variantCount; ++v )
			variant += pick >= g_variants[v].x ? 1 : 0;
	const float4 placement = g_placement[variant];

	float3 angles = ( float3( random( cell, seed + 4 ), random( cell, seed + 5 ), random( cell, seed + 6 ) ) - 0.5f ) * g_rotationRange;

	// Широкая модель (куртина травы) — ещё и по следу основания: корень на бровке, а пучки куртины над врезом русла висели
	// бы в воздухе (как проверка границ точки в PCG UE). Размер — наибольший при этой ячейке, след поворачивается с моделью
	const float4 footprint = g_footprints[variant];
	const float sizeVariation = placement.z >= 0.0f ? placement.z : g_sizeVariation;
	const float sizeBound = g_sizeMultiplier * placement.y * lerp( 1.0f - sizeVariation, 1.0f, random( cell, seed + 3 ) );
	const float footprintRadius = footprint.z * sizeBound;
	if( footprintRadius > footprintCheckRadius )
	{
		float sinYaw, cosYaw;
		sincos( angles.y, sinYaw, cosYaw );
		// Поворот вокруг Y как у quaternionAxisAngle( Y ): x' = x cos + z sin, z' = −x sin + z cos
		const float2 center = worldXZ + float2( footprint.x * cosYaw + footprint.y * sinYaw,
												-footprint.x * sinYaw + footprint.y * cosYaw ) * sizeBound;
		float clear = foliageClear( center );
		clear = max( clear, foliageClear( center + float2( footprintRadius, 0.0f ) ) );
		clear = max( clear, foliageClear( center - float2( footprintRadius, 0.0f ) ) );
		clear = max( clear, foliageClear( center + float2( 0.0f, footprintRadius ) ) );
		clear = max( clear, foliageClear( center - float2( 0.0f, footprintRadius ) ) );
		// Только решение «ставить ли»: размер — по плотности в корне, как у модели без проверки следа
		if( chance >= density * ( 1.0f - clear ) )
			return false;
	}

	item.position = displaced ? terrainSurfacePoint( worldXZ ) : float3( worldXZ.x, terrainHeight( worldXZ ), worldXZ.y );
	// Размер: размер и разброс слоя и варианта (Size, Size variation) и плотность маски
	item.size = sizeBound * lerp( 0.6f, 1.0f, density );

	item.rotation = quaternionMul( quaternionAxisAngle( float3( 0.0f, 1.0f, 0.0f ), angles.y ),
								   quaternionMul( quaternionAxisAngle( float3( 1.0f, 0.0f, 0.0f ), angles.x ),
												  quaternionAxisAngle( float3( 0.0f, 0.0f, 1.0f ), angles.z ) ) );
	if( placement.w >= 0.0f ? placement.w > 0.5f : g_alignToTerrain > 0.5f )
		item.rotation = quaternionMul( quaternionFromTo( float3( 0.0f, 1.0f, 0.0f ), terrainNormal( worldXZ ) ), item.rotation );

	// Посадка по следу основания (Ground fit, как Embed у PCG UE): под широким основанием (пень, бревно) земля неровная, и по
	// одной точке в центре край основания висел над ямкой. Модель — до самой низкой точки земли под следом (центр и восемь
	// точек по его краю) относительно плоскости, на которую её ставит наклон (по земле в центре или ровно), и глубже на
	// Ground embed × размер
	[branch] if( g_groundFit > 0.5f )
	{
		const bool aligned = placement.w >= 0.0f ? placement.w > 0.5f : g_alignToTerrain > 0.5f;
		const float3 normal = aligned ? terrainNormal( worldXZ ) : float3( 0.0f, 1.0f, 0.0f );
		float sinYaw, cosYaw;
		sincos( angles.y, sinYaw, cosYaw );
		const float2 center = worldXZ + float2( footprint.x * cosYaw + footprint.y * sinYaw,
												-footprint.x * sinYaw + footprint.y * cosYaw ) * item.size;
		const float radius = footprint.z * item.size;
		static const float2 rim[9] = { float2( 0.0f, 0.0f ), float2( 1.0f, 0.0f ), float2( -1.0f, 0.0f ), float2( 0.0f, 1.0f ),
									   float2( 0.0f, -1.0f ), float2( 0.7071f, 0.7071f ), float2( -0.7071f, 0.7071f ),
									   float2( 0.7071f, -0.7071f ), float2( -0.7071f, -0.7071f ) };
		float lowest = 0.0f;
		[unroll] for( uint k = 0; k < 9; ++k )
		{
			const float2 sample = center + rim[k] * radius;
			const float2 offset = sample - worldXZ;
			const float plane = item.position.y - ( normal.x * offset.x + normal.z * offset.y ) / max( normal.y, 0.2f );
			lowest = min( lowest, groundHeight( sample, displaced ) - plane );
		}
		item.position.y += lowest - g_groundEmbed * item.size;
	}

	// Флаговые деревья (на открытых гребнях): где сдувает — наклон по ветру уровня (туда, куда он дует; деревья растут по
	// господствующему ветру — не по полю кадра и не по тому, включён ли ветер сейчас) и ниже
	[branch] if( g_ridgeLean > 0.0f || g_ridgeShrink > 0.0f )
	{
		const float exposure = saturate( g_windExposure.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).r );
		float sinLean, cosLean;
		sincos( g_ridgeLean * exposure, sinLean, cosLean );
		const float3 leaned = float3( cb_windDirection.x * sinLean, cosLean, cb_windDirection.y * sinLean );
		item.rotation = quaternionMul( quaternionFromTo( float3( 0.0f, 1.0f, 0.0f ), leaned ), item.rotation );
		item.size *= 1.0f - g_ridgeShrink * exposure;
	}

	// Дальности LOD — свои у экземпляра: у модели, ближе на долю по случайному числу ячейки и шуму мира
	lodScale = 1.0f - LOD_JITTER_RANDOM * random( cell, seed + 8 ) -
			   LOD_JITTER_NOISE * ( worldNoise( worldXZ, LOD_JITTER_NOISE_SIZE, seed + 9 ) * 0.5f + 0.5f );
	return true;
}

// Дерево на дальних горах в ячейке (слой far_forest): только за краем карты (на ней — свой лес); плотность — леса гор по их
// правилам, у края карты — маска слоя (лес карты), продолженная наружу; высота — земли гор (как у их вершин у камеры); без
// наклона по земле, следа и цвета земли — деревья вдали
bool placeFarTree( int2 cell, uint seed, out ScatterItem item, out uint variant, out float lodScale )
{
	item = (ScatterItem)0;
	variant = 0;
	lodScale = 1.0f;
	if( !farGroundExists() )
		return false;
	const float2 offset = ( float2( random( cell, seed ), random( cell, seed + 1 ) ) - 0.5f ) * g_jitter;
	const float2 worldXZ = ( (float2)cell + 0.5f + offset ) * g_cellSize;
	const float2 uv = terrainUV( worldXZ );
	if( all( uv >= 0.0f ) && all( uv <= 1.0f ) )
		return false;
	const float density = farForestDensity( worldXZ, g_densityMask.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).r );
	if( random( cell, seed + 2 ) >= density )
		return false;
	const float pick = random( cell, seed + 7 );
	[loop] for( uint v = 0; v + 1 < g_variantCount; ++v )
		variant += pick >= g_variants[v].x ? 1 : 0;
	const float4 placement = g_placement[variant];
	const float3 angles = ( float3( random( cell, seed + 4 ), random( cell, seed + 5 ), random( cell, seed + 6 ) ) - 0.5f ) * g_rotationRange;
	const float sizeVariation = placement.z >= 0.0f ? placement.z : g_sizeVariation;
	item.size = g_sizeMultiplier * placement.y * lerp( 1.0f - sizeVariation, 1.0f, random( cell, seed + 3 ) ) * lerp( 0.6f, 1.0f, density );
	item.position = float3( worldXZ.x, farGroundHeight( worldXZ, terrainHeight( worldXZ ) ), worldXZ.y );
	item.rotation = quaternionMul( quaternionAxisAngle( float3( 0.0f, 1.0f, 0.0f ), angles.y ),
								   quaternionMul( quaternionAxisAngle( float3( 1.0f, 0.0f, 0.0f ), angles.x ),
												  quaternionAxisAngle( float3( 0.0f, 0.0f, 1.0f ), angles.z ) ) );
	lodScale = 1.0f - LOD_JITTER_RANDOM * random( cell, seed + 8 ) -
			   LOD_JITTER_NOISE * ( worldNoise( worldXZ, LOD_JITTER_NOISE_SIZE, seed + 9 ) * 0.5f + 0.5f );
	return true;
}

// Сфера экземпляра: по границам модели варианта (у деревьев — десятки метров) и не меньше прежней «единичной» сферы
// размера size над точкой (трава, цветы — модели меньше метра); высота — для длины тени
void instanceSphere( ScatterItem item, uint variant, out float3 center, out float radius, out float height )
{
	const float4 bounds = g_bounds[variant];
	enclose( item.position + float3( 0.0f, item.size * 0.5f, 0.0f ), item.size,
			 item.position + quaternionRotate( bounds.xyz, item.rotation ) * item.size, bounds.w * item.size, center, radius );
	height = max( item.size, ( bounds.y + bounds.w ) * item.size );
}

// Пара экземпляров перехода подряд в пуле перехода: уходящий LOD с долей t, приходящий с t − 1. index — пара экземпляра
// (0xffffffff — ещё не взята, 0xfffffffe — пул полон): у экземпляра она одна на долю перехода, общая для видов. false —
// места нет
bool reserveTransition( ScatterItem item, float transition, inout uint index )
{
	if( index == 0xffffffffU )
	{
		uint pair;
		g_counters.InterlockedAdd( COUNTER_TRANSITIONS, 2, pair );
		if( pair + 1 < g_transitionCapacity )
		{
			ScatterTransitionItem leaving;
			leaving.item = item;
			leaving.lodDither = transition;
			leaving.padding = 0.0f;
			g_transitions[pair] = leaving;
			leaving.lodDither = transition - 1.0f;
			g_transitions[pair + 1] = leaving;
			index = pair;
		}
		else
		{
			g_counters.InterlockedAdd( COUNTER_TRANSITIONS, 0xfffffffeU );
			index = 0xfffffffeU;
		}
	}
	return index != 0xfffffffeU;
}

// Экземпляр у края кольца, исчезающий дизерингом: одно место в пуле перехода с долей fadeOut (остаются пиксели с порогом
// выше неё — Shaders/lod_dither.sh), общее для видов. index — как у reserveTransition
bool reserveFade( ScatterItem item, float fadeOut, inout uint index )
{
	if( index == 0xffffffffU )
	{
		uint slot;
		if( reserve( COUNTER_TRANSITIONS, g_transitionCapacity, slot ) )
		{
			ScatterTransitionItem fading;
			fading.item = item;
			fading.lodDither = fadeOut;
			fading.padding = 0.0f;
			g_transitions[slot] = fading;
			index = slot;
		}
		else
			index = 0xfffffffeU;
	}
	return index != 0xfffffffeU;
}

// Экземпляр — в пул кадра и списки видов, где он виден: видимость по видам кадра (views — какие виды вообще проверять),
// LOD вида и переход дизерингом. fadeOut (0; 1) — экземпляр у края кольца исчезает долей пикселей (g_ditherFade): в
// списке перехода своего LOD, смена LOD в этой полосе — без перехода. false — пул полон
bool emitInstance( ScatterItem item, uint variant, float lodScale, float distanceToCamera, uint views, float fadeOut )
{
	// Видимость по видам. Главный вид — сфера вокруг инстанса в его frustum. Каскады теней (у слоя с тенью): инстанс
	// нужен им, только если он или его тень попадает в кадр — сфера, охватывающая путь луча от него до земли
	// (frustum каскада вдоль света охватывает и много лишнего); тень ложится на землю не дальше её длины от инстанса
	// вдоль света, поэтому каскад, полоса глубины взгляда которого не пересекается с глубиной сферы инстанса,
	// протянутой вдоль света, инстанс не рисует (тень солнца сбоку или со спины не уходит в ближние каскады); и сам
	// frustum каскада. Съёмка сцены с другой камеры — своим frustum, LOD — как у главного вида
	float3 center;
	float radius;
	float height;
	instanceSphere( item, variant, center, radius, height );
	const float shadowLength = height * g_shadowCast.w;
	const bool mainVisible = ( views & 1u ) != 0 && insideFrustum( 0, center, radius );
	uint visibleMask = mainVisible ? 1u : 0u;
	// Съёмка сцены — своей пирамидой видимости, без главного вида
	[loop] for( uint capture = 1; capture < g_viewCount; ++capture )
	{
		if( ( views & ( 1u << capture ) ) != 0 && captureView( capture ) )
			visibleMask |= insideFrustum( capture, center, radius ) ? 1u << capture : 0;
	}
	if( g_castShadow > 0.5f && g_viewCount > 1 &&
		( mainVisible || ( shadowLength > 0.0f && insideFrustum( 0, center + g_shadowCast.xyz * ( shadowLength * 0.5f ), radius + shadowLength * 0.5f ) ) ) )
	{
		const float depthCenter = dot( center - cb_cameraPosition, cb_viewDirection );
		const float shadowDepth = shadowLength * dot( g_shadowCast.xyz, cb_viewDirection );
		const float depthNear = depthCenter + min( shadowDepth, 0.0f ) - radius;
		const float depthFar = depthCenter + max( shadowDepth, 0.0f ) + radius;
		[loop] for( uint view = 1; view < g_viewCount; ++view )
		{
			if( ( views & ( 1u << view ) ) == 0 || !shadowView( view ) )
				continue;
			if( depthFar < g_viewDepths[view].x || depthNear > g_viewDepths[view].y )
				continue;
			visibleMask |= insideFrustum( view, center, radius ) ? 1u << view : 0;
		}
	}
	if( visibleMask == 0 )
		return true;
	const uint viewCount = g_viewCount;

	const uint lodCount = (uint)g_variants[variant].y;
	const bool dithered = g_variants[variant].z > 0.5f;
	const float4 lodEnd = g_lodEnd[variant] * lodScale;
	const float4 shadowLodEnd = g_shadowLodEnd[variant] * lodScale;
	// Свои дальности у теней — свой выбор LOD, и долю перехода с главным видом не разделить: переход без дизеринга
	const bool ownShadowLods = any( g_shadowLodEnd[variant].xyz != g_lodEnd[variant].xyz );
	const uint shadowLods = (uint)g_shadowCasting[variant].x;
	const float shadowLastLod = g_variants[variant].w * lodScale;
	// Расстояние до точки LOD — у всех видов кадра одна, главная камера (RenderView::lodOrigin)
	const float distanceToLodOrigin = distanceToCamera;

	// Место в пулах берётся один раз, когда какой-нибудь вид попросил: обычный экземпляр и пара экземпляров перехода
	// (доля у всех видов с переходом одна — множитель LOD у них 1); у видов теней — ещё пара перехода к последнему LOD
	uint itemIndex = 0xffffffffU;
	uint transitionIndex = 0xffffffffU;
	uint shadowTransitionIndex = 0xffffffffU;
	uint fadeIndex = 0xffffffffU;
	[loop] for( uint view = 0; view < viewCount; ++view )
	{
		if( ( visibleMask & ( 1u << view ) ) == 0 )
			continue;
		uint lod;
		float transition;
		const bool shadowLod = shadowView( view );
		const bool viewDithered = dithered && !( shadowLod && ownShadowLods );
		selectLod( distanceToLodOrigin, shadowLod ? shadowLodEnd : lodEnd, lodCount, viewDithered, lod, transition );
		// LOD, с которого у теней экземпляра нет (импостер без тени): в полосе перехода к нему — только уходящий LOD
		const uint castingLods = shadowLod ? shadowLods : lodCount;

		// Исчезает у края кольца: долей пикселей своего LOD; нет места в пуле перехода — обычным списком целиком
		[branch] if( fadeOut > 0.0f )
		{
			if( shadowLod && shadowLastLod > 0.0f && distanceToLodOrigin >= shadowLastLod )
				lod = lodCount - 1;
			if( lod >= castingLods )
				continue;
			if( reserveFade( item, fadeOut, fadeIndex ) )
			{
				pushIndex( view, listIndex( variant, lod, true ), fadeIndex );
				continue;
			}
			transition = 0.0f;
		}

		// Тень дальнего экземпляра — последним LOD раньше, чем в кадре (импостер леса); в полосе вокруг этой дальности —
		// переход дизерингом от LOD кадра к последнему своей парой: тень меняется так же плавно, как экземпляр в кадре
		if( shadowLod && shadowLastLod > 0.0f && lod + 1 < lodCount )
		{
			const float start = shadowLastLod;
			const float halfBand = dithered ? start * ( LOD_TRANSITION_WIDTH * 0.5f ) : 0.0f;
			if( distanceToLodOrigin >= start + halfBand )
			{
				lod = lodCount - 1;
				transition = 0.0f;
			}
			else if( distanceToLodOrigin > start - halfBand )
			{
				const float shadowTransition = ( distanceToLodOrigin - ( start - halfBand ) ) / ( 2.0f * halfBand );
				if( reserveTransition( item, shadowTransition, shadowTransitionIndex ) )
				{
					if( lod < castingLods )
						pushIndex( view, listIndex( variant, lod, true ), shadowTransitionIndex );
					if( lodCount - 1 < castingLods )
						pushIndex( view, listIndex( variant, lodCount - 1, true ), shadowTransitionIndex + 1 );
					continue;
				}
				transition = 0.0f;
			}
		}
		if( lod >= castingLods )
			continue;

		// Нет места в пуле перехода — экземпляр в этом виде идёт обычным списком своего LOD
		if( transition > 0.0f && reserveTransition( item, transition, transitionIndex ) )
		{
			pushIndex( view, listIndex( variant, lod, true ), transitionIndex );
			if( lod + 1 < castingLods )
				pushIndex( view, listIndex( variant, lod + 1, true ), transitionIndex + 1 );
			continue;
		}

		if( itemIndex == 0xffffffffU )
		{
			uint slot;
			if( reserve( COUNTER_ITEMS, g_itemCapacity, slot ) )
			{
				g_items[slot] = item;
				itemIndex = slot;
			}
			else
				return false;	// пул полон: ёмкость слоя мала (ScatterPass::capacity)
		}
		pushIndex( view, listIndex( variant, lod, false ), itemIndex );
	}
	return true;
}

// Доля полосы исчезания, за которую экземпляр с рваным краем сходит в ноль размером: исчезают по одному, а не все разом, и
// не кочками полного размера у самого порога; дизерингом — дольше: доля пикселей сразу не видна
static const float edgeShrink = 0.35f;
static const float edgeDither = 0.6f;
// Насколько крупнее остаются экземпляры при прореживании: размер × доля оставшихся ^ −thinGrowth (0,5 — покрытие то же, но
// пучок растёт и в высоту — сверху дальние пучки кочками; 0,25 — половина покрытия возвращается)
static const float thinGrowth = 0.25f;

// Своё случайное число экземпляра 0…1 — у кольцевого слоя по ячейке, у постоянного по его месту (раскладка одна и та же)
float instanceRandom( int2 cell, uint seed, uint index )
{
	return random( cell, seed + index );
}
float instanceRandom( float3 position, uint seed, uint index )
{
	return random( int2( asint( position.x ), asint( position.z ) ), seed + index );
}

// Исчезание у краёв кольца: множитель размера по расстоянию до камеры. С g_edgeSpread у каждого экземпляра своя дальность
// исчезания в полосе g_farFade (g_nearFade — у ближней): его случайное число и крупный шум мира (40 и 13 м — граница рваная,
// а не ровная дуга вокруг камеры, как случайная дальность отсечения экземпляра у травы и Foliage в UE); размер сходит в ноль
// только у самого порога. Ячейка та же, поэтому без мерцания и «пересадки»; 0 — все уменьшаются вместе по всей полосе
float ringFade( float distanceToCamera, float random01, float2 worldXZ )
{
	float threshold = 0.0f;
	[branch] if( g_edgeSpread > 0.0f )
	{
		const float noise = worldNoise( worldXZ, 40.0f, 13 ) * 0.6f + worldNoise( worldXZ, 13.0f, 14 ) * 0.4f;
		threshold = g_edgeSpread * saturate( 0.65f * random01 + 0.35f * ( 0.5f + 0.5f * noise ) );
	}
	const float width = lerp( 1.0f, g_ditherFade > 0.5f ? edgeDither : edgeShrink, g_edgeSpread );
	const float farEnd = g_farBorder - g_farFade * threshold;
	const float nearStart = g_nearBorder + g_nearFade * threshold;
	return saturate( ( farEnd - distanceToCamera ) / max( g_farFade * width, 1e-3f ) ) *
		   saturate( ( distanceToCamera - nearStart ) / max( g_nearFade * width, 1e-3f ) );
}

// Исчезание у края кольца: размером (size × fade) или, с g_ditherFade и вариантом со сменой LOD дизерингом, долей пикселей —
// экземпляр полного размера проступает из прозрачности (как PerInstanceFadeAmount у Foliage в UE), без «вырастания» травы.
// Возвращает долю исчезнувших пикселей (0 — весь), 1 — экземпляра нет
float edgeFade( float fade, uint variant, inout float size )
{
	if( fade <= 0.0f )
		return 1.0f;
	if( g_ditherFade > 0.5f && g_variants[variant].z > 0.5f )
		return 1.0f - fade;
	size *= fade;
	return 0.0f;
}

// Прореживание вдали (как редеющие дальние тайлы травы Ghost of Tsushima и Density Scaling в UE): доля оставшихся — от 1 у
// g_thinStart до 1 − g_thinning у дальней границы; уходят одни и те же (порог — своё случайное число экземпляра), а
// оставшиеся немного крупнее (thinGrowth) — луг редеет меньше. Экземпляр не пропадает разом (с 14 см шага пучок посреди
// луга возникал целиком), а исчезает, пока доля уходит ниже его числа на THINNING_SOFTNESS: множитель 0…1 — как у края
// кольца (edgeFade: дизерингом или размером); 0 — экземпляр убран. Число экземпляра — в 0…1 − THINNING_SOFTNESS: до
// g_thinStart (доля 1) все целиком, иначе экземпляры с числом у единицы просвечивали бы дизерингом по всему кольцу
float thinOut( float distanceToCamera, float random01, inout float size )
{
	[branch] if( g_thinning <= 0.0f || g_thinStart >= g_farBorder )
		return 1.0f;
	const float keep = 1.0f - g_thinning * smoothstep( g_thinStart, g_farBorder, distanceToCamera );
	size *= pow( max( keep, 0.05f ), -thinGrowth );
	return saturate( ( keep - random01 * ( 1.0f - THINNING_SOFTNESS ) ) / THINNING_SOFTNESS );
}

// Цвет земли под экземпляром кольцевого слоя (scatter_ground.sh): средний цвет слоёв террейна в точке с их множителем и
// пятнами яркости — растение берёт его у корня, вдали (вторая половина кольца) — и выше; свой цвет — с теми же пятнами
// яркости и разбросом по экземплярам (по случайному числу ячейки: у экземпляра всегда тот же)
uint4 groundColor( float3 position, float distanceToCamera, int2 cell, uint seed )
{
	if( g_groundColor <= 0.0f && g_groundColorFar <= 0.0f && g_colorVariation <= 0.0f )
		return 0;
	const float3 ground = g_groundColor > 0.0f || g_groundColorFar > 0.0f ? terrainGroundColor( position.xz ) : 0.0f;
	const float brightness = terrainBrightness( position.xz ) * ( 1.0f + g_colorVariation * ( 2.0f * random( cell, seed + 10 ) - 1.0f ) );
	const float far = g_groundColorFar * smoothstep( 0.5f * g_farBorder, g_farBorder, distanceToCamera );
	return packScatterGround( ground, brightness, g_groundColor, g_groundColorHeight, far );
}

// Кольцевой слой: ячейки квадрата 2 · far вокруг камеры
[numthreads( 32, 32, 1 )]
void main( uint3 dispatchThreadId : SV_DispatchThreadID )
{
	if( dispatchThreadId.x >= g_gridDim || dispatchThreadId.y >= g_gridDim )
		return;

	// Первая ячейка — у угла квадрата 2 · far вокруг камеры; у слоёв с разными параметрами разный узор
	int2 firstCell = (int2)floor( ( cb_cameraPosition.xz - g_farBorder ) / g_cellSize );
	int2 cell = firstCell + (int2)dispatchThreadId.xy;
	uint seed = asuint( g_cellSize ) ^ hash( asuint( g_farBorder ) ^ hash( asuint( g_sizeMultiplier ) ) );

	ScatterItem item;
	uint variant;
	float lodScale;
	const bool farTree = g_farForest > 0.5f;
	// На земле со смещением дисплейсмента (камни слоёв у камеры), и посадка по следу — по ней же: прежде точку ставили на
	// смещённую землю уже после посадки, и та пропадала — ветка висела краем над землёй (Dimok, 2026-10-21). Деревья на
	// горах — на земле гор
	if( farTree ? !placeFarTree( cell, seed, item, variant, lodScale ) : !placeInCell( cell, seed, true, item, variant, lodScale ) )
		return;

	float distanceToCamera = distance( item.position, cb_cameraPosition );
	if( distanceToCamera < g_nearBorder || distanceToCamera > g_farBorder )
		return;
	const float thin = thinOut( distanceToCamera, instanceRandom( cell, seed, 12 ), item.size );
	if( thin <= 0.0f )
		return;

	const float fade = ringFade( distanceToCamera, instanceRandom( cell, seed, 11 ), item.position.xz ) * thin;
	const float fadeOut = edgeFade( fade, variant, item.size );
	if( item.size <= 0.0f || fadeOut >= 1.0f )
		return;
	item.ground = groundColor( item.position, distanceToCamera, cell, seed );

	// Кольцо вокруг главной камеры — не в съёмку сцены с другой камеры
	emitInstance( item, variant, lodScale, distanceToCamera, ~captureViews(), fadeOut );
}

// Постоянный слой, раз при загрузке: группа — кластер PLACED_CLUSTER_CELLS² ячеек сетки мира от начала координат.
// Экземпляры кластера — подряд в g_placed, место берётся одним атомарным сложением на группу
groupshared uint gs_placedCount;
groupshared uint gs_placedFirst;
groupshared uint gs_minHeight;
groupshared uint gs_maxHeight;

[numthreads( PLACED_CLUSTER_CELLS, PLACED_CLUSTER_CELLS, 1 )]
void placeWorld( uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID, uint threadIndex : SV_GroupIndex )
{
	if( threadIndex == 0 )
	{
		gs_placedCount = 0;
		gs_minHeight = 0xffffffffU;
		gs_maxHeight = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	const int2 cell = (int2)( groupId.xy * PLACED_CLUSTER_CELLS + threadId.xy );
	const uint seed = asuint( g_cellSize ) ^ hash( asuint( g_farBorder ) ^ hash( asuint( g_sizeMultiplier ) ) );
	PlacedItem placed = (PlacedItem)0;
	const bool valid = placeInCell( cell, seed, false, placed.item, placed.variant, placed.lodScale );
	uint local = 0;
	if( valid )
	{
		float3 center;
		float radius;
		float height;
		instanceSphere( placed.item, placed.variant, center, radius, height );
		InterlockedAdd( gs_placedCount, 1, local );
		InterlockedMin( gs_minHeight, orderedFloat( min( placed.item.position.y, center.y - radius ) ) );
		InterlockedMax( gs_maxHeight, orderedFloat( center.y + radius ) );
	}
	GroupMemoryBarrierWithGroupSync();

	const uint cluster = groupId.y * g_clustersX + groupId.x;
	if( threadIndex == 0 )
	{
		uint first = 0;
		if( gs_placedCount > 0 )
			g_placedCounter.InterlockedAdd( 0, gs_placedCount, first );
		// Пул полон — кластер пуст (ёмкость — по числу ячеек, так не бывает)
		const uint count = first + gs_placedCount <= g_placedCapacity ? gs_placedCount : 0;
		gs_placedFirst = first;
		g_clusters[cluster] = uint4( first, count, gs_minHeight, gs_maxHeight );
	}
	GroupMemoryBarrierWithGroupSync();

	if( valid && gs_placedFirst + gs_placedCount <= g_placedCapacity )
		g_placed[gs_placedFirst + local] = placed;
}

// Постоянный слой, каждый кадр: группа — кластер. Сначала кластер целиком (его ящик — сферой) по видам кадра и кольцу
// дальности, затем его экземпляры — как у кольцевого слоя, только в видах, где виден кластер
groupshared uint gs_clusterViews;

[numthreads( 64, 1, 1 )]
void cullPlaced( uint3 groupId : SV_GroupID, uint threadIndex : SV_GroupIndex )
{
	const uint cluster = groupId.x;
	if( cluster >= g_clusterCount )
		return;
	const uint4 range = g_clusterRanges[cluster];
	if( range.y == 0 )
		return;

	if( threadIndex == 0 )
	{
		const float clusterSize = PLACED_CLUSTER_CELLS * g_cellSize;
		const float2 cornerXZ = float2( cluster % g_clustersX, cluster / g_clustersX ) * clusterSize;
		const float3 boxMin = float3( cornerXZ.x - g_clusterMargin, unorderedFloat( range.z ), cornerXZ.y - g_clusterMargin );
		const float3 boxMax = float3( cornerXZ.x + clusterSize + g_clusterMargin, unorderedFloat( range.w ), cornerXZ.y + clusterSize + g_clusterMargin );
		const float3 center = ( boxMin + boxMax ) * 0.5f;
		const float radius = length( boxMax - boxMin ) * 0.5f;
		// Расстояния от камеры до ближней и дальней точки ящика: вне кольца — кластер не нужен ни одному виду
		const float nearest = length( max( max( boxMin - cb_cameraPosition, cb_cameraPosition - boxMax ), 0.0f ) );
		const float farthest = length( max( abs( boxMin - cb_cameraPosition ), abs( boxMax - cb_cameraPosition ) ) );
		uint views = 0;
		if( nearest <= g_farBorder && farthest >= g_nearBorder )
		{
			views = insideFrustum( 0, center, radius ) ? 1u : 0u;
			// Съёмка сцены — своей пирамидой видимости (кольцо — то же, вокруг главной камеры)
			[loop] for( uint capture = 1; capture < g_viewCount; ++capture )
				views |= captureView( capture ) && insideFrustum( capture, center, radius ) ? 1u << capture : 0;
			if( g_castShadow > 0.5f )
			{
				// Тень кластера ложится не дальше её длины от него: высота кластера на метр высоты вдоль луча
				const float shadowLength = ( boxMax.y - boxMin.y ) * g_shadowCast.w;
				[loop] for( uint view = 1; view < g_viewCount; ++view )
				{
					if( !shadowView( view ) || farthest + shadowLength < g_viewParams[view].z || nearest - shadowLength > g_viewParams[view].w )
						continue;
					views |= insideFrustum( view, center, radius ) ? 1u << view : 0;
				}
			}
		}
		gs_clusterViews = views;
	}
	GroupMemoryBarrierWithGroupSync();

	const uint views = gs_clusterViews;
	if( views == 0 )
		return;
	const uint seed = asuint( g_cellSize ) ^ hash( asuint( g_farBorder ) ^ hash( asuint( g_sizeMultiplier ) ) );
	[loop] for( uint i = threadIndex; i < range.y; i += 64 )
	{
		const PlacedItem placed = g_placedItems[range.x + i];
		ScatterItem item = placed.item;
		const float distanceToCamera = distance( item.position, cb_cameraPosition );
		if( distanceToCamera < g_nearBorder || distanceToCamera > g_farBorder )
			continue;
		const float thin = thinOut( distanceToCamera, instanceRandom( item.position, seed, 12 ), item.size );
		if( thin <= 0.0f )
			continue;
		const float fade = ringFade( distanceToCamera, instanceRandom( item.position, seed, 11 ), item.position.xz ) * thin;
		const float fadeOut = edgeFade( fade, placed.variant, item.size );
		if( item.size <= 0.0f || fadeOut >= 1.0f )
			continue;
		if( !emitInstance( item, placed.variant, placed.lodScale, distanceToCamera, views, fadeOut ) )
			return;
	}
}

// После раскладки: команды ExecuteIndirect по видам и группам. Поток — секция списка вида
[numthreads( 64, 1, 1 )]
void buildCommands( uint3 id : SV_DispatchThreadID )
{
	const uint total = g_viewCount * MAX_LISTS * MAX_SECTIONS;
	if( id.x >= total )
		return;
	const uint view = id.x / ( MAX_LISTS * MAX_SECTIONS );
	const uint list = ( id.x / MAX_SECTIONS ) % MAX_LISTS;
	const uint section = id.x % MAX_SECTIONS;

	const uint count = min( g_counters.Load( listCountOffset( view, list ) ), g_lists[list].y );
	if( count == 0 )
		return;
	const uint4 args = g_sectionArgs[list * MAX_SECTIONS + section];
	if( args.w == 0 )
		return;
	const uint group = args.w - 1;
	uint slot;
	g_counters.InterlockedAdd( groupCountOffset( view, group ), 1, slot );
	if( slot >= g_groups[group].y )
		return;

	// {начало списка индексов в g_indices (root-константа b9), IndexCountPerInstance, InstanceCount, StartIndexLocation,
	// BaseVertexLocation, StartInstanceLocation}
	const uint offset = ( view * MAX_GROUPS + g_groups[group].x + slot ) * COMMAND_STRIDE;
	g_commands.Store( offset, view * g_indexStride + g_lists[list].w );
	g_commands.Store4( offset + 4, uint4( args.x, count, args.y, args.z ) );
	g_commands.Store( offset + 20, 0 );
}
