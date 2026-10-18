// Скиннинг вершины (define SKINNED, перестановка ShaderPermutation::skinned) — как GPU skin vertex factory в UE: точка и
// направления вершины в позе кадра — смесь до 4 матриц скиннинга её костей (обратная привязка × место кости в
// пространстве модели: AnimationPose.h, skinningMatrices). Матрицы — буфер вызова t14 (SkinningPalette): на кость три
// float4 — строки транспонированной 3 × 4; первая кость объекта — cb_boneOffset (константы объекта), у инстансного вызова
// (INST_MATRIX: стадо одним вызовом) — своя у каждого экземпляра. Номера костей и веса — третий поток вершин
// (VertexData::Skin: uint8 и unorm8 с суммой 1). instance.sh — до этого файла
#ifndef SKINNING_SH
#define SKINNING_SH

#ifdef SKINNED

#include "slots.h"
#include "bindless.sh"

DM_SRV( StructuredBuffer<float4>, g_skinMatrices, SLOT_SKIN_MATRICES );

// Первая кость экземпляра (instanceIndex — номер в данных инстансов, instanceSlot)
uint skinBoneOffset( uint instanceIndex )
{
#ifdef INST_MATRIX
	return g_instanceTransforms[instanceIndex].extra.x;
#else
	return cb_boneOffset;
#endif
}

float3x4 skinMatrix( uint4 joints, float4 weights, uint boneOffset )
{
	float3x4 result = (float3x4)0;
	[unroll] for( uint i = 0; i < 4; ++i )
	{
		const uint row = ( boneOffset + joints[i] ) * 3;
		result += weights[i] * float3x4( g_skinMatrices[row], g_skinMatrices[row + 1], g_skinMatrices[row + 2] );
	}
	return result;
}

float3 skinPoint( float3x4 skin, float3 position )
{
	return mul( skin, float4( position, 1.0f ) );
}

// Направление (нормаль, касательная): масштаб у костей равномерный — без обратной транспонированной, длину правит normalize
float3 skinDirection( float3x4 skin, float3 direction )
{
	return mul( (float3x3)skin, direction );
}

#endif

#endif
