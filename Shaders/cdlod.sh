////////////////////////////////////////////////////////////////////////////////
// Общее для шейдеров террейна CDLOD (cdlod.vs, terrain.ps, cdlod_lod.ps): параметры террейна
// (CDLODBuffer — cdlod_constants.h), карта высот и выход вершинного шейдера. Константы слоёв, нужные и тем, кто стоит на земле
// (повтор, размах смещения, смешивание, точка LOD), — ресурс сцены в terrain_layers.sh
////////////////////////////////////////////////////////////////////////////////

#ifndef CDLOD_SH
#define CDLOD_SH

#include "slots.h"

#include "samplers.sh"
#include "bindless.sh"
#include "terrain_detail.sh"
#include "terrain_layer_constants.h"

// Константы слоёв terrain_layers.sh — из cbuffer террейна, а не из буфера сцены
#define TERRAIN_LAYERS_IN_CBUFFER

#include "cdlod_constants.h"

// Копия карты высот в R32_FLOAT с полной цепочкой мипов, в вершинном и пиксельном шейдерах
DM_SRV( Texture2D, g_heightMap, 0 );

struct PixelInputType
{
	float4 position : SV_POSITION;
	float3 worldPosition : WORLDPOS0;	// со смещением (дисплейсмент): свет, тень, туман
	float3 basePosition : WORLDPOS1;	// без смещения: текстуры, веса и проекции слоёв (текстура не плывёт по смещённой земле)
	float2 uv : TEXCOORD0;			// координаты карты высот и splat-карты
	float2 lodDebug : TEXCOORD1;	// x — уровень LOD, y — коэффициент морфинга
};

// Ось z мира идёт по текстуре снизу вверх
float2 heightMapUV( float2 worldXZ )
{
	precise float2 uv = worldXZ / g_worldSize;
	uv.y = 1.0f - uv.y;
	return uv;
}

// precise: вершины стыка карты и дальних гор (cdlod_patch.sh) считают её в разных шейдерах — результат совпадает до бита
float sampleHeight( float2 worldXZ, float mip )
{
	precise float2 uv = heightMapUV( worldXZ );
	precise float height = g_heightMap.SampleLevel( g_SamplerLinearClamp, uv, mip ).r * g_heightMultiplier + g_heightOffset;
	return height;
}

// Высота самой детальной земли в точке: плитка у русла (terrain_detail.sh) или карта высот (мип 0)
float sampleFineHeight( float2 worldXZ )
{
	float detail;
	precise float height;
	if( sampleTerrainDetail( worldXZ, g_detailTile, detail ) )
		height = detail * g_heightMultiplier + g_heightOffset;
	else
		height = sampleHeight( worldXZ, 0.0f );
	return height;
}

// Нормаль рельефа: центральные разности в мипе карты высот, тексель которого примерно равен пикселю экрана.
// Геометрия вдали грубее карты высот, но освещение по такой нормали остаётся детальным
float3 terrainNormal( float2 worldXZ, float mip )
{
	float offset = g_texelSize * exp2( mip );
	float left  = sampleHeight( worldXZ - float2( offset, 0.0f ), mip );
	float right = sampleHeight( worldXZ + float2( offset, 0.0f ), mip );
	float back  = sampleHeight( worldXZ - float2( 0.0f, offset ), mip );
	float front = sampleHeight( worldXZ + float2( 0.0f, offset ), mip );
	return normalize( float3( left - right, 2.0f * offset, back - front ) );
}

// Нормаль по самой детальной земле (плитка у русла или карта высот) — центральные разности через offset, м
float3 fineNormal( float2 worldXZ, float offset )
{
	float left  = sampleFineHeight( worldXZ - float2( offset, 0.0f ) );
	float right = sampleFineHeight( worldXZ + float2( offset, 0.0f ) );
	float back  = sampleFineHeight( worldXZ - float2( 0.0f, offset ) );
	float front = sampleFineHeight( worldXZ + float2( 0.0f, offset ) );
	return normalize( float3( left - right, 2.0f * offset, back - front ) );
}

#endif
