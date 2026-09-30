////////////////////////////////////////////////////////////////////////////////
// Расстановка слоя набора (Scatterer) — растения: трава, цветы, камешки. Инстансы раскладываются по сетке с шагом g_cellSize,
// привязанной к миру, в кольце g_nearBorder…g_farBorder вокруг камеры. Смещение в ячейке, размер и поворот — хеш
// координат ячейки, поэтому при движении камеры инстансы остаются на своих местах. Маска слоя задаёт вероятность
// появления и размер, инстансы вне frustum отбрасываются (у слоя с тенью — если и тень не падает в кадр), у краёв
// кольца размер плавно уходит в ноль. Модель инстанса — один из вариантов слоя по весам (как Mesh Entries у Static
// Mesh Spawner в PCG UE), по случайному числу ячейки: варианты делят сетку и не пересекаются. Инстанс попадает в список
// «вариант × LOD» по расстоянию: позиция, поворот и размер от LOD не зависят, поэтому при смене LOD у растения
// меняется только меш. Дальность LOD у каждого экземпляра своя — дальности модели со сдвигом по случайному числу ячейки
// и шуму мира (Shaders/lod_transition.h), поэтому граница LOD не идёт дугой; у варианта со сменой LOD дизерингом
// экземпляр в полосе перехода попадает в списки перехода обоих LOD с долей перехода (Dithered LOD Transition в UE,
// Shaders/lod_dither.sh). Секции LOD (меши со своими материалами) рисуют тот же список: число инстансов считается в записи
// аргументов секции 0, после расстановки copySectionCounts переносит его в записи остальных секций.
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
	float  g_castShadow;		// 1 — слой отбрасывает тень солнца
	uint   g_variantCount;		// моделей слоя, 1…8
	float3 g_layerPadding;
};

// ScatterPass::VariantsBuffer; списки — ScatterPass::listIndex, записи аргументов — ScatterPass::argsOffset
static const uint maxLods = 4;
static const uint maxVariants = 8;
#define MAX_SECTIONS 4	// ScatterPass::maxSections
#define MAX_LISTS 64	// ScatterPass::maxLists: обычные списки пар «вариант × LOD», затем списки перехода
cbuffer ScatterVariantsBuffer : register( b7 )
{
	float4 g_variants[maxVariants];	// x — накопленная доля варианта (0…1), y — число LOD, z — 1: смена LOD дизерингом
	float4 g_lodEnd[maxVariants];	// дальности LOD 0…2 варианта, м: дальше — следующий LOD
	uint4  g_lists[MAX_LISTS];		// x — начало списка в его буфере (g_instances или g_transitions), y — ёмкость, z — число секций
};

uint listIndex( uint variant, uint lod, bool transition )
{
	return ( transition ? maxVariants * maxLods : 0 ) + variant * maxLods + lod;
}

// Запись indirect-аргументов секции списка в g_drawArgs, байты (по 20 на запись)
uint argsOffset( uint list, uint section )
{
	return ( list * MAX_SECTIONS + section ) * 20;
}

// Нормированные плоскости frustum, нормали смотрят внутрь
cbuffer FrustumBuffer : register( b6 )
{
	float4 g_frustumPlanes[6];
	float4 g_shadowCast;	// xyz — куда идёт свет солнца, w — длина тени на метр высоты вдоль луча (0 — солнца нет)
};

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

DM_UAV( RWByteAddressBuffer, g_drawArgs, 0 );	// аргументы DrawIndexedInstancedIndirect по секциям списков; число инстансов — по смещению 4
DM_UAV( RWStructuredBuffer<ScatterItem>, g_instances, 1 );	// списки «вариант × LOD» подряд, участки — g_lists
DM_UAV( RWStructuredBuffer<ScatterTransitionItem>, g_transitions, 2 );	// списки перехода подряд
DM_SRV( Texture2D, g_densityMask, 2 );

// После расстановки: число инстансов списка — в записях всех его секций (поток — список)
[numthreads( MAX_LISTS, 1, 1 )]
void copySectionCounts( uint3 id : SV_DispatchThreadID )
{
	const uint list = id.x;
	const uint count = g_drawArgs.Load( argsOffset( list, 0 ) + 4 );
	for( uint section = 1; section < g_lists[list].z; ++section )
		g_drawArgs.Store( argsOffset( list, section ) + 4, count );
}

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

// Место в списке: счётчик — число инстансов в indirect-аргументах секции 0. Список полон — счётчик возвращается,
// чтобы отрисовка не читала за концом списка
bool reserve( uint list, out uint index )
{
	const uint countOffset = argsOffset( list, 0 ) + 4;
	g_drawArgs.InterlockedAdd( countOffset, 1, index );
	if( index < g_lists[list].y )
		return true;
	g_drawArgs.InterlockedAdd( countOffset, 0xffffffffU );
	return false;
}

void writeTransition( uint list, uint index, ScatterItem item, float lodDither )
{
	ScatterTransitionItem transition;
	transition.item = item;
	transition.lodDither = lodDither;
	transition.padding = 0.0f;
	g_transitions[g_lists[list].x + index] = transition;
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

bool insideFrustum( float3 center, float radius )
{
	[unroll]
	for( uint i = 0; i < 6; ++i )
	{
		if( dot( g_frustumPlanes[i].xyz, center ) + g_frustumPlanes[i].w < -radius )
			return false;
	}
	return true;
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
	float3 center = position + float3( 0.0f, size * 0.5f, 0.0f );
	bool visible = insideFrustum( center, size );
	// Инстанс за краем кадра может отбросить тень в кадр: проверяется и сфера, охватывающая путь луча от него до земли
	[branch] if( !visible && g_castShadow > 0.5f && g_shadowCast.w > 0.0f )
	{
		float shadowLength = size * g_shadowCast.w;
		visible = insideFrustum( center + g_shadowCast.xyz * ( shadowLength * 0.5f ), size + shadowLength * 0.5f );
	}
	if( !visible )
		return;

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

	// LOD — как у моделей уровня: первый, чья дальность не меньше расстояния; последний — до конца кольца. Дальности —
	// свои у экземпляра: у модели, ближе на долю по случайному числу ячейки и шуму мира. Со сменой LOD дизерингом
	// в полосе дальность × (1 ± LOD_TRANSITION_WIDTH / 2) экземпляр переходит к следующему LOD, transition — доля
	const uint lodCount = (uint)g_variants[variant].y;
	const bool dithered = g_variants[variant].z > 0.5f;
	const float lodScale = 1.0f - LOD_JITTER_RANDOM * random( cell, seed + 8 ) -
						   LOD_JITTER_NOISE * ( worldNoise( worldXZ, LOD_JITTER_NOISE_SIZE, seed + 9 ) * 0.5f + 0.5f );
	const float4 lodEnd = g_lodEnd[variant] * lodScale;
	uint lod = 0;
	float transition = 0.0f;
	[unroll] for( uint i = 0; i < 3; ++i )
	{
		const float halfBand = dithered ? lodEnd[i] * ( LOD_TRANSITION_WIDTH * 0.5f ) : 0.0f;
		if( lod == i && i + 1 < lodCount && distanceToCamera > lodEnd[i] - halfBand )
		{
			if( distanceToCamera >= lodEnd[i] + halfBand )
				lod = i + 1;
			else
				transition = ( distanceToCamera - ( lodEnd[i] - halfBand ) ) / ( 2.0f * halfBand );
		}
	}

	ScatterItem item;
	item.position = position;
	item.size = size;
	item.rotation = rotation;

	uint index;
	if( transition > 0.0f )
	{
		// В полосе — в списки перехода обоих LOD: уходящий с долей t, приходящий с t − 1. Нет места у приходящего —
		// уходящий рисуется целиком (доля 0), у уходящего — экземпляр идёт в обычный список своего LOD
		uint nextIndex;
		if( reserve( listIndex( variant, lod, true ), index ) )
		{
			const bool next = reserve( listIndex( variant, lod + 1, true ), nextIndex );
			writeTransition( listIndex( variant, lod, true ), index, item, next ? transition : 0.0f );
			if( next )
				writeTransition( listIndex( variant, lod + 1, true ), nextIndex, item, transition - 1.0f );
			return;
		}
	}

	const uint list = listIndex( variant, lod, false );
	if( reserve( list, index ) )
		g_instances[g_lists[list].x + index] = item;
}
