#pragma once

#include <vector>
#include <DirectXMath.h>
#include "Skeleton.h"

namespace GS
{

// Поза — локальные места всех костей скелета (JointTransform, как FCompactPose в UE). Смешение поз, места костей в
// пространстве модели и матрицы скиннинга для вершинного шейдера (Shaders/skinning.sh)

// Между a (weight = 0) и b (weight = 1): перемещение и масштаб — линейно, поворот — nlerp по кратчайшей дуге (как
// FTransform::Blend в UE: на малых углах соседних кадров и поз от slerp не отличить)
JointTransform blendJoint( const JointTransform& a, const JointTransform& b, float weight );
void blendPoses( const std::vector<JointTransform>& a, const std::vector<JointTransform>& b, float weight,
				 std::vector<JointTransform>& out );
// Поза покоя скелета
void restPose( const Skeleton& skeleton, std::vector<JointTransform>& out );

// Матрица места кости: масштаб, поворот, перемещение (точка-строка × матрица, как в DirectXMath)
DirectX::XMMATRIX jointMatrix( const JointTransform& joint );
// Места костей в пространстве модели: своё место × место родителя (родитель — раньше потомка)
void modelSpace( const Skeleton& skeleton, const std::vector<JointTransform>& local, std::vector<DirectX::XMFLOAT4X4>& model );
// Матрицы скиннинга — обратная привязка × место кости в пространстве модели, транспонированные 3 × 4 (три столбца аффинной
// матрицы): вершинный шейдер берёт три float4 и точку — dot( строка, float4( p, 1 ) ). out — jointCount матриц
void skinningMatrices( const Skeleton& skeleton, const std::vector<DirectX::XMFLOAT4X4>& model, DirectX::XMFLOAT3X4* out );

}
