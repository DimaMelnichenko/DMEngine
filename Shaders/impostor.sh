////////////////////////////////////////////////////////////////////////////////
// Импостер дерева (ImpostorMaterial) — как octahedral impostors в UE (ImpostorBaker, R. Brucks): виды модели с
// g_impostorFrames² направлений верхней полусферы (сетка на полуоктаэдре), каждый — срез массива текстур (цвет и покрытие;
// нормаль модели и доля пропускания; глубина поверхности). В кадре — карточка, развёрнутая к виду; три кадра, ближайших к
// направлению на зрителя, смешиваются по весам; пиксель освещается в точке запечённой поверхности и пишет её глубину
// (pixel depth offset, как у импостеров UE). Общее для impostor.vs, impostor.ps и запекания (ImpostorMaterial::bake)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_IMPOSTOR_SH
#define DM_IMPOSTOR_SH

#include "slots.h"

#include "impostor_constants.h"

// Выход вершинного шейдера: карточка перед сферой (position), её точка в плоскости вида через центр сферы
// (worldPosition), UV в трёх кадрах, их срезы и веса, поворот экземпляра; для точки поверхности — направление на
// зрителя и радиус сферы в мире, расстояние точки worldPosition от плоскости через центр поперёк направления на зрителя
struct ImpostorPixelInput
{
	// centroid: пиксельный шейдер с SV_DepthLessEqual читает position.z (без MSAA — центр пикселя, как обычно)
	precise centroid float4 position : SV_POSITION;
	precise float3 worldPosition : TEXCOORD0;
	float4 frameUV01 : TEXCOORD1;
	float2 frameUV2 : TEXCOORD2;
	nointerpolation uint3 frames : TEXCOORD3;
	nointerpolation float3 weights : TEXCOORD4;
	nointerpolation float4 rotation : TEXCOORD5;	// кватернион экземпляра: нормаль модели — в мир
	nointerpolation float4 viewer : TEXCOORD6;		// xyz — направление на зрителя, w — радиус сферы, м
	precise float planeOffset : TEXCOORD7;
	nointerpolation float2 heightRange : TEXCOORD9;	// x — высота начала координат модели в мире, y — 1 / высота модели, м
	nointerpolation uint4 ground : TEXCOORD10;		// цвет земли под экземпляром (scatter_ground.sh)
	nointerpolation float4 center : TEXCOORD11;		// xyz — центр сферы модели в мире, w — масштаб экземпляра
#ifdef LOD_DITHER
	nointerpolation float lodDither : TEXCOORD8;
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

// UV точки offset (координаты модели, от центра сферы) в кадре frame
float2 impostorFrameUV( uint2 frame, float3 offset )
{
	float3 right, up;
	impostorFrameBasis( impostorFrameDirection( frame ), right, up );
	const float radius = g_impostorBounds.w;
	return float2( 0.5f + 0.5f * dot( offset, right ) / radius, 0.5f - 0.5f * dot( offset, up ) / radius );
}

// Три кадра сетки вокруг направления (координаты модели; снизу кадров нет — направление прижимается к горизонту):
// треугольник сетки, в котором оно лежит, и барицентрические веса
void impostorFrameTriangle( float3 direction, out uint2 frame0, out uint2 frame1, out uint2 frame2, out float3 weights )
{
	direction.y = max( direction.y, 1e-3f );
	direction = normalize( direction );
	const uint frames = g_impostorFrames;
	const float2 grid = ( hemiOctEncode( direction ) * 0.5f + 0.5f ) * float( frames - 1 );
	const uint2 base = min( (uint2)max( floor( grid ), 0.0f ), frames - 2 );
	const float2 f = saturate( grid - float2( base ) );
	if( f.x + f.y < 1.0f )
	{
		frame0 = base;
		frame1 = base + uint2( 1, 0 );
		frame2 = base + uint2( 0, 1 );
		weights = float3( 1.0f - f.x - f.y, f.x, f.y );
	}
	else
	{
		frame0 = base + uint2( 1, 1 );
		frame1 = base + uint2( 0, 1 );
		frame2 = base + uint2( 1, 0 );
		weights = float3( f.x + f.y - 1.0f, 1.0f - f.x, 1.0f - f.y );
	}
}

#endif
