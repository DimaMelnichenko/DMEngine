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
// порядке и не одним ли инстансным вызовом с одинаковыми будет нарисован, решает рендерер
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

	std::vector<Instance> m_instances;
	DirectX::BoundingBox m_bounds;
	PropertyContainer m_properties;
};

}
