////////////////////////////////////////////////////////////////////////////////
// Расстановка слоя набора (Scatterer): трава, цветы, камешки. Инстансы раскладываются по сетке с шагом g_cellSize,
// привязанной к миру, в кольце g_nearBorder…g_farBorder вокруг камеры. Смещение в ячейке, размер и поворот — хеш
// координат ячейки, поэтому при движении камеры инстансы остаются на своих местах. Маска слоя задаёт вероятность
// появления и размер, инстансы вне frustum отбрасываются, у краёв кольца размер плавно уходит в ноль.
// init — сбрасывает indirect-аргументы слоя перед расстановкой
////////////////////////////////////////////////////////////////////////////////

#include "samplers.sh"
#include "common.vs"
#include "terrain_height.sh"

// DMComputeShader::Dispatch: b_rect — размер сетки в ячейках
cbuffer ThreadsData : register( b2 )
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
	float3 argsPadding;
};

// ScatterPass::PopulateParams
cbuffer ScatterLayerBuffer : register( b4 )
{
	float  g_nearBorder;
	float  g_farBorder;
	float  g_nearFade;
	float  g_farFade;
	float  g_sizeMultipler;
	float  g_cellSize;
	float  g_jitter;			// смещение внутри ячейки, доля шага
	float  g_alignToTerrain;	// 1 — ось Y инстанса по нормали террейна
	float3 g_rotationRange;		// предел случайного поворота вокруг осей X, Y, Z, радианы
	uint   g_capacity;
};

// Нормированные плоскости frustum, нормали смотрят внутрь
cbuffer FrustumBuffer : register( b6 )
{
	float4 g_frustumPlanes[6];
};

// Совпадает с InstanceParam в instance.sh при INST_POS, INST_SCALE и INST_ROTATE
struct ScatterItem
{
	float3 position;
	float  size;
	float4 rotation;	// кватернион
};

RWByteAddressBuffer g_drawArgs : register( u0 );	// аргументы DrawIndexedInstancedIndirect, число инстансов по смещению 4
RWStructuredBuffer<ScatterItem> g_instances : register( u1 );
Texture2D g_densityMask : register( t2 );

[numthreads( 1, 1, 1 )]
void init()
{
	g_drawArgs.Store4( 0, uint4( indexCountPerInstance, instanceCount, startIndexLocation, (uint)baseVertexLocation ) );
	g_drawArgs.Store( 16, startInstanceLocation );
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
	uint seed = asuint( g_cellSize ) ^ hash( asuint( g_farBorder ) ^ hash( asuint( g_sizeMultipler ) ) );

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
	float size = g_sizeMultipler * lerp( 0.7f, 1.0f, random( cell, seed + 3 ) ) * lerp( 0.6f, 1.0f, density );
	size *= saturate( ( g_farBorder - distanceToCamera ) / max( g_farFade, 1e-3f ) );
	size *= saturate( ( distanceToCamera - g_nearBorder ) / max( g_nearFade, 1e-3f ) );

	if( size <= 0.0f || !insideFrustum( position + float3( 0.0f, size * 0.5f, 0.0f ), size ) )
		return;

	float3 angles = ( float3( random( cell, seed + 4 ), random( cell, seed + 5 ), random( cell, seed + 6 ) ) - 0.5f ) * g_rotationRange;
	float4 rotation = quaternionMul( quaternionAxisAngle( float3( 0.0f, 1.0f, 0.0f ), angles.y ),
									 quaternionMul( quaternionAxisAngle( float3( 1.0f, 0.0f, 0.0f ), angles.x ),
													quaternionAxisAngle( float3( 0.0f, 0.0f, 1.0f ), angles.z ) ) );
	if( g_alignToTerrain > 0.5f )
		rotation = quaternionMul( quaternionFromTo( float3( 0.0f, 1.0f, 0.0f ), terrainNormal( worldXZ ) ), rotation );

	uint index;
	g_drawArgs.InterlockedAdd( 4, 1, index );
	if( index >= g_capacity )
	{
		// Буфер полон: счётчик возвращается, чтобы отрисовка не читала за концом буфера
		g_drawArgs.InterlockedAdd( 4, 0xffffffffU );
		return;
	}

	ScatterItem item;
	item.position = position;
	item.size = size;
	item.rotation = rotation;
	g_instances[index] = item;
}
