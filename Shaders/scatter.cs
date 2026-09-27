////////////////////////////////////////////////////////////////////////////////
// Расстановка слоя набора (Scatterer) — растения: трава, цветы, камешки. Инстансы раскладываются по сетке с шагом g_cellSize,
// привязанной к миру, в кольце g_nearBorder…g_farBorder вокруг камеры. Смещение в ячейке, размер и поворот — хеш
// координат ячейки, поэтому при движении камеры инстансы остаются на своих местах. Маска слоя задаёт вероятность
// появления и размер, инстансы вне frustum отбрасываются (у слоя с тенью — если и тень не падает в кадр), у краёв
// кольца размер плавно уходит в ноль. Модель инстанса — один из вариантов слоя по весам (как Mesh Entries у Static
// Mesh Spawner в PCG UE), по случайному числу ячейки: варианты делят сетку и не пересекаются. Инстанс попадает в список
// «вариант × LOD» по расстоянию: позиция, поворот и размер от LOD не зависят, поэтому при смене LOD у растения
// меняется только меш.
// init — сбрасывает indirect-аргументы одного списка перед расстановкой
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#include "samplers.sh"
#include "common.vs"
#include "terrain_height.sh"

// DMComputeShader::Dispatch: b_rect — размер сетки в ячейках
cbuffer ThreadsData : register( SLOT_CB_PASS )
{
	float  b_groupDim;
	float2 b_rect;
	float  b_elapsedTime;
};

cbuffer ArgsBuffer : register( b3 )
{
	uint   indexCountPerInstance;
	uint   instanceCount;
	uint   startIndexLocation;
	int    baseVertexLocation;
	uint   startInstanceLocation;
	uint   argsOffset;			// аргументы списка в g_drawArgs, байты
	float2 argsPadding;
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

// ScatterPass::VariantsBuffer; списки — ScatterPass::listIndex
static const uint maxLods = 4;
cbuffer ScatterVariantsBuffer : register( b7 )
{
	float4 g_variants[8];		// x — накопленная доля варианта (0…1), y — число LOD
	float4 g_lodEnd[8];			// дальности LOD 0…2 варианта, м: дальше — следующий LOD
	uint4  g_lists[32];			// x — начало списка в g_instances, y — ёмкость
};

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

RWByteAddressBuffer g_drawArgs : register( u0 );	// аргументы DrawIndexedInstancedIndirect по спискам, по 20 байт; число инстансов — по смещению 4
RWStructuredBuffer<ScatterItem> g_instances : register( u1 );	// списки «вариант × LOD» подряд, участки — g_lists
Texture2D g_densityMask : register( t2 );

[numthreads( 1, 1, 1 )]
void init()
{
	g_drawArgs.Store4( argsOffset, uint4( indexCountPerInstance, instanceCount, startIndexLocation, (uint)baseVertexLocation ) );
	g_drawArgs.Store( argsOffset + 16, startInstanceLocation );
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

	// LOD — как у моделей уровня: первый, чья дальность не меньше расстояния; последний — до конца кольца
	uint lodCount = (uint)g_variants[variant].y;
	float4 lodEnd = g_lodEnd[variant];
	uint lod = 0;
	[unroll] for( uint i = 0; i < 3; ++i )
		lod += ( i + 1 < lodCount && distanceToCamera > lodEnd[i] ) ? 1 : 0;
	uint list = variant * maxLods + lod;
	uint countOffset = list * 20 + 4;

	uint index;
	g_drawArgs.InterlockedAdd( countOffset, 1, index );
	if( index >= g_lists[list].y )
	{
		// Список полон: счётчик возвращается, чтобы отрисовка не читала за концом списка
		g_drawArgs.InterlockedAdd( countOffset, 0xffffffffU );
		return;
	}

	ScatterItem item;
	item.position = position;
	item.size = size;
	item.rotation = rotation;
	g_instances[g_lists[list].x + index] = item;
}
