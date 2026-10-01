////////////////////////////////////////////////////////////////////////////////
// Импостер дерева (ImpostorMaterial) — как octahedral impostors в UE (ImpostorBaker, R. Brucks): виды модели с
// g_impostorFrames² направлений верхней полусферы (сетка на полуоктаэдре), каждый — срез массива текстур (цвет и покрытие;
// нормаль модели и доля пропускания). В кадре — карточка, развёрнутая к виду; три кадра, ближайших к направлению на
// зрителя, смешиваются по весам. Общее для impostor.vs, impostor.ps и запекания (ImpostorMaterial::bake)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_IMPOSTOR_SH
#define DM_IMPOSTOR_SH

#include "slots.h"

// ImpostorMaterial::Params
cbuffer ImpostorBuffer : register( SLOT_CB_MATERIAL )
{
	float4 g_impostorBounds;		// сфера модели: xyz — центр в её координатах, w — радиус
	float4 g_impostorTransmission;	// xyz — цвет пропускания (множитель альбедо), w — множитель доли
	uint   g_impostorFrames;		// кадров по стороне сетки
	float  g_impostorAlphaCutoff;
	float  g_impostorRoughness;
	float  g_impostorPadding;
};

// Выход вершинного шейдера: точка карточки в мире, UV в трёх кадрах, их срезы и веса, поворот экземпляра
struct ImpostorPixelInput
{
	precise float4 position : SV_POSITION;
	float3 worldPosition : TEXCOORD0;
	float4 frameUV01 : TEXCOORD1;
	float2 frameUV2 : TEXCOORD2;
	nointerpolation uint3 frames : TEXCOORD3;
	nointerpolation float3 weights : TEXCOORD4;
	nointerpolation float4 rotation : TEXCOORD5;	// кватернион экземпляра: нормаль модели — в мир
#ifdef LOD_DITHER
	nointerpolation float lodDither : TEXCOORD6;
#endif
};

float3 impostorRotate( float3 v, float4 q )
{
	return v + 2.0f * cross( q.xyz, cross( q.xyz, v ) + q.w * v );
}

// Направление верхней полусферы (y ≥ 0) ↔ точка квадрата [−1; 1]² (полуоктаэдр): углы квадрата — горизонт, центр — зенит
float2 hemiOctEncode( float3 d )
{
	d /= dot( abs( d ), 1.0f );
	return float2( d.x + d.z, d.x - d.z );
}

float3 hemiOctDecode( float2 p )
{
	const float x = ( p.x + p.y ) * 0.5f;
	const float z = ( p.x - p.y ) * 0.5f;
	return normalize( float3( x, 1.0f - abs( x ) - abs( z ), z ) );
}

// Направление кадра сетки (x, y — номер по сторонам), откуда смотрела камера запекания
float3 impostorFrameDirection( uint2 frame )
{
	return hemiOctDecode( float2( frame ) / float( g_impostorFrames - 1 ) * 2.0f - 1.0f );
}

// Базис кадра — как у XMMatrixLookAtLH запекания: камера со стороны direction смотрит на центр
void impostorFrameBasis( float3 direction, out float3 right, out float3 up )
{
	const float3 forward = -direction;
	const float3 worldUp = abs( direction.y ) > 0.999f ? float3( 0.0f, 0.0f, 1.0f ) : float3( 0.0f, 1.0f, 0.0f );
	right = normalize( cross( worldUp, forward ) );
	up = cross( forward, right );
}

#endif
