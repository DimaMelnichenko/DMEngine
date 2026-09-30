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
// После раскладки buildCommands собирает команды ExecuteIndirect: по виду и группе (секции с одним материалом и
// состоянием — ScatterPass::groups) подряд команды {начало списка индексов (root-константа b9), DrawIndexedInstanced},
// число команд группы — счётчик в g_counters. Один ExecuteIndirect на группу на вид вместо вызова на каждую секцию
// каждого списка; счётчики списков — там же.
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#include "samplers.sh"
#include "common.vs"
#include "terrain_height.sh"
#include "lod_transition.h"
#include "bindless.sh"

// DMComputeShader::Dispatch: b_rect — размер сетки в ячейках
cbuffer ThreadsData : register( SLOT_CB_PASS )
{
	float  b_groupDim;
	float2 b_rect;
	float  b_elapsedTime;
};

// ScatterPass::PopulateParams
cbuffer ScatterLayerBuffer : register( b4 )
{
	float  g_nearBorder;
	float  g_farBorder;
	float  g_nearFade;
	float  g_farFade;
	float  g_sizeMultiplier;
	float  g_cellSize;
	float  g_jitter;			// смещение внутри ячейки, доля шага
	float  g_alignToTerrain;	// 1 — ось Y инстанса по нормали террейна
	float3 g_rotationRange;		// предел случайного поворота вокруг осей X, Y, Z, радианы
	float  g_castShadow;		// 1 — слой отбрасывает тень солнца: инстансы попадают и в списки видов теней
	uint   g_variantCount;		// моделей слоя, 1…8
	uint   g_itemCapacity;		// ёмкость пула инстансов g_items
	uint   g_transitionCapacity;// ёмкость пула перехода g_transitions
	uint   g_indexStride;		// индексов на вид в g_indices (списки всех пар вида подряд)
};

// ScatterPass::VariantsBuffer; списки — ScatterPass::listIndex
static const uint maxLods = 4;
static const uint maxVariants = 8;
#define MAX_SECTIONS 4	// ScatterPass::maxSections
#define MAX_LISTS 64	// ScatterPass::maxLists: обычные списки пар «вариант × LOD», затем списки перехода
#define MAX_VIEWS 5		// maxRenderViews: главный вид и каскады теней
#define MAX_GROUPS 256	// ScatterPass::maxGroups — не больше секций списков
cbuffer ScatterVariantsBuffer : register( b7 )
{
	float4 g_variants[maxVariants];	// x — накопленная доля варианта (0…1), y — число LOD, z — 1: смена LOD дизерингом
	float4 g_lodEnd[maxVariants];	// дальности LOD 0…2 варианта, м: дальше — следующий LOD
	uint4  g_lists[MAX_LISTS];		// y — ёмкость списка индексов (на вид), z — число секций, w — начало списка в индексах вида
};

// Виды кадра: плоскости frustum каждого (нормированные, нормали внутрь) и параметры вида
cbuffer FrustumBuffer : register( b6 )
{
	float4 g_frustumPlanes[MAX_VIEWS * 6];
	// x — множитель дальностей LOD (< 1 — LOD грубее), y — 1: списки перехода у вида; у каскада теней z, w — полоса
	// расстояний от камеры (RenderView::cascadeNear / cascadeFar): тень инстанса ложится не дальше её длины от него
	float4 g_viewParams[MAX_VIEWS];
	float4 g_shadowCast;	// xyz — куда идёт свет источника теней, w — длина тени на метр высоты вдоль луча (0 — теней нет)
	uint   g_viewCount;
	uint3  g_frustumPadding;
};

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

[numthreads( 32, 32, 1 )]
void main( uint3 dispatchThreadId : SV_DispatchThreadID )
{
	if( dispatchThreadId.x >= (uint)b_rect.x || dispatchThreadId.y >= (uint)b_rect.y )
		return;

	// Первая ячейка — у угла квадрата 2 · far вокруг камеры; у слоёв с разными параметрами разный узор
	int2 firstCell = (int2)floor( ( cb_cameraPosition.xz - g_farBorder ) / g_cellSize );
	int2 cell = firstCell + (int2)dispatchThreadId.xy;
	uint seed = asuint( g_cellSize ) ^ hash( asuint( g_farBorder ) ^ hash( asuint( g_sizeMultiplier ) ) );

	float2 offset = ( float2( random( cell, seed ), random( cell, seed + 1 ) ) - 0.5f ) * g_jitter;
	float2 worldXZ = ( (float2)cell + 0.5f + offset ) * g_cellSize;

	float2 uv = terrainUV( worldXZ );
	if( any( uv < 0.0f ) || any( uv > 1.0f ) )
		return;

	float density = g_densityMask.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).r;
	if( random( cell, seed + 2 ) >= density )
		return;

	float3 position = float3( worldXZ.x, terrainHeight( worldXZ ), worldXZ.y );
	float distanceToCamera = distance( position, cb_cameraPosition );
	if( distanceToCamera < g_nearBorder || distanceToCamera > g_farBorder )
		return;

	// Размер: разброс, плотность маски и плавное исчезание у краёв кольца
	float size = g_sizeMultiplier * lerp( 0.7f, 1.0f, random( cell, seed + 3 ) ) * lerp( 0.6f, 1.0f, density );
	size *= saturate( ( g_farBorder - distanceToCamera ) / max( g_farFade, 1e-3f ) );
	size *= saturate( ( distanceToCamera - g_nearBorder ) / max( g_nearFade, 1e-3f ) );

	if( size <= 0.0f )
		return;

	// Видимость по видам. Главный вид — сфера вокруг инстанса в его frustum. Каскады теней (у слоя с тенью): инстанс
	// нужен им, только если он или его тень попадает в кадр — сфера, охватывающая путь луча от него до земли
	// (frustum каскада вдоль света охватывает и много лишнего); тень ложится на землю не дальше её длины от инстанса,
	// поэтому каскад, полоса расстояний которого с этим не пересекается, инстанс не рисует; и сам frustum каскада
	const float3 center = position + float3( 0.0f, size * 0.5f, 0.0f );
	const float shadowLength = size * g_shadowCast.w;
	const bool mainVisible = insideFrustum( 0, center, size );
	uint visibleMask = mainVisible ? 1u : 0u;
	if( g_castShadow > 0.5f && g_viewCount > 1 &&
		( mainVisible || ( shadowLength > 0.0f && insideFrustum( 0, center + g_shadowCast.xyz * ( shadowLength * 0.5f ), size + shadowLength * 0.5f ) ) ) )
	{
		[loop] for( uint view = 1; view < g_viewCount; ++view )
		{
			if( distanceToCamera + shadowLength < g_viewParams[view].z || distanceToCamera - shadowLength > g_viewParams[view].w )
				continue;
			visibleMask |= insideFrustum( view, center, size ) ? 1u << view : 0;
		}
	}
	if( visibleMask == 0 )
		return;
	const uint viewCount = g_viewCount;

	float3 angles = ( float3( random( cell, seed + 4 ), random( cell, seed + 5 ), random( cell, seed + 6 ) ) - 0.5f ) * g_rotationRange;
	float4 rotation = quaternionMul( quaternionAxisAngle( float3( 0.0f, 1.0f, 0.0f ), angles.y ),
									 quaternionMul( quaternionAxisAngle( float3( 1.0f, 0.0f, 0.0f ), angles.x ),
													quaternionAxisAngle( float3( 0.0f, 0.0f, 1.0f ), angles.z ) ) );
	if( g_alignToTerrain > 0.5f )
		rotation = quaternionMul( quaternionFromTo( float3( 0.0f, 1.0f, 0.0f ), terrainNormal( worldXZ ) ), rotation );

	// Вариант — по накопленным долям весов; отдельное случайное число ячейки (прежние 0…6 от вариантов не зависят)
	float pick = random( cell, seed + 7 );
	uint variant = 0;
	[loop] for( uint v = 0; v + 1 < g_variantCount; ++v )
		variant += pick >= g_variants[v].x ? 1 : 0;

	// Дальности LOD — свои у экземпляра: у модели, ближе на долю по случайному числу ячейки и шуму мира
	const uint lodCount = (uint)g_variants[variant].y;
	const bool dithered = g_variants[variant].z > 0.5f;
	const float lodScale = 1.0f - LOD_JITTER_RANDOM * random( cell, seed + 8 ) -
						   LOD_JITTER_NOISE * ( worldNoise( worldXZ, LOD_JITTER_NOISE_SIZE, seed + 9 ) * 0.5f + 0.5f );
	const float4 lodEnd = g_lodEnd[variant] * lodScale;
	// Расстояние до точки LOD — у всех видов кадра одна, главная камера (RenderView::lodOrigin)
	const float distanceToLodOrigin = distanceToCamera;

	ScatterItem item;
	item.position = position;
	item.size = size;
	item.rotation = rotation;

	// Место в пулах берётся один раз, когда какой-нибудь вид попросил: обычный экземпляр и пара экземпляров перехода
	// (уходящий LOD с долей t, приходящий с t − 1; доля у всех видов с переходом одна — множитель LOD у них 1)
	uint itemIndex = 0xffffffffU;
	uint transitionIndex = 0xffffffffU;
	[loop] for( uint view = 0; view < viewCount; ++view )
	{
		if( ( visibleMask & ( 1u << view ) ) == 0 )
			continue;
		uint lod;
		float transition;
		const bool viewDithered = dithered && g_viewParams[view].y > 0.5f;
		selectLod( distanceToLodOrigin, lodEnd * g_viewParams[view].x, lodCount, viewDithered, lod, transition );

		if( transition > 0.0f )
		{
			if( transitionIndex == 0xffffffffU )
			{
				// Пара экземпляров перехода подряд: нет места — экземпляр в этом виде идёт обычным списком своего LOD
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
					transitionIndex = pair;
				}
				else
				{
					g_counters.InterlockedAdd( COUNTER_TRANSITIONS, 0xfffffffeU );
					transitionIndex = 0xfffffffeU;	// пул перехода полон
				}
			}
			if( transitionIndex != 0xfffffffeU )
			{
				pushIndex( view, listIndex( variant, lod, true ), transitionIndex );
				pushIndex( view, listIndex( variant, lod + 1, true ), transitionIndex + 1 );
				continue;
			}
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
				return;	// пул полон: ёмкость слоя мала (ScatterPass::capacity)
		}
		pushIndex( view, listIndex( variant, lod, false ), itemIndex );
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
