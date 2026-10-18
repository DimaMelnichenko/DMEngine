#pragma once

#include <cstdint>
#include <vector>
#include <DirectXMath.h>
#include "D3D\DMStructuredBuffer.h"

namespace GS
{

class Skeleton;

// Кость позы отрезком в мире: от родителя к кости (отладочный вид «Skeleton», рисует вьюпорт)
struct BoneLine
{
	DirectX::XMFLOAT3 from;
	DirectX::XMFLOAT3 to;
};

// Кости позы (места костей в пространстве модели, AnimationPose.h: modelSpace) — отрезками в мире по мировой матрице модели
void appendBoneLines( const Skeleton& skeleton, const std::vector<DirectX::XMFLOAT4X4>& model, DirectX::FXMMATRIX world,
					  std::vector<BoneLine>& lines );

// Матрицы скиннинга кадра у объекта сцены (WorldEntities, ModelInstances) — как буфер костей Skeletal Mesh в UE: позы всех
// видимых моделей со скелетом подряд в одном структурном буфере кольца кадра. Модель берёт место под свои кости (append →
// сдвиг первой, MeshBatch::boneOffset), объект загружает кадр один раз (upload — в compute, до отрисовки), рендерер
// привязывает буфер к вызову меша (bind, слот SLOT_SKIN_MATRICES): вершинный шейдер с SKINNED читает сдвиг + номер кости
class SkinningPalette
{
public:
	void clear() { m_matrices.clear(); }
	// Место под jointCount матриц (AnimationPose.h: skinningMatrices пишет сюда), offset — номер первой
	DirectX::XMFLOAT3X4* append( size_t jointCount, uint32_t& offset );
	bool empty() const { return m_matrices.empty(); }
	size_t size() const { return m_matrices.size(); }
	// Записать кадр в кольцо: после всех append, до отрисовки
	void upload();
	void bind() const;

private:
	std::vector<DirectX::XMFLOAT3X4> m_matrices;
	mutable DMStructuredBuffer m_buffer;
	size_t m_capacity = 0;
};

}
