#pragma once

#include <vector>
#include <DirectXCollision.h>
#include "SceneObject.h"
#include "Model\DMModel.h"
#include "Common\DMTransform.h"
#include "ObjectLibrary\LevelDescription.h"

namespace GS
{

// Модели уровня (таблица LevelModels): у каждого экземпляра свой трансформ, модель — общий ресурс. За каждый вид
// выбирает LOD экземпляра по расстоянию от точки LOD вида, отсекает его по frustum вида (границы меша в мировых
// координатах) и отдаёт его меш (MeshBatch); в какой проход он попадёт (непрозрачные, полупрозрачные), в каком
// порядке и не одним ли инстансным вызовом с одинаковыми будет нарисован, решает рендерер. В полосе вокруг дальности
// LOD (Shaders/lod_transition.h) экземпляр отдаёт оба LOD с долями дизеринга, если материал секции это умеет
// (DitheredLODTransition, как Dithered LOD Transition в UE); за последним LOD он так же исчезает
class ModelInstances : public SceneObject
{
public:
	ModelInstances();

	// Вызывается после загрузки моделей: экземпляры уровня и свойства их моделей для GUI
	void initialize( const std::vector<LevelDescription::ModelInstance>& instances );

	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	PropertyContainer* properties() override;
	// Границы всех экземпляров (меш LOD 0 в мировых координатах); false — экземпляров нет
	bool bounds( DirectX::BoundingBox& bounds ) const;

private:
	struct Instance
	{
		DMModel* model;
		uint32_t modelId;
		DMTransform transform;
	};

	// Секции LOD экземпляра в список вида. lodDither — доля перехода (MeshBatch::lodDither): (0; 1) — уходящий LOD,
	// (−1; 0) — приходящий. Секция без дизеринга рисуется, только пока её сторона перехода — не меньше половины: LOD
	// сменяется мгновенно на дальности модели, как без полосы
	void addLod( const Instance& instance, uint16_t lodIndex, float lodDither, float distance, const RenderView& view,
				 MeshCollector& collector ) const;

	std::vector<Instance> m_instances;
	DirectX::BoundingBox m_bounds;
	PropertyContainer m_properties;
};

}
