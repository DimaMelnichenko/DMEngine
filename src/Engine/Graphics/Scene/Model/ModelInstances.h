#pragma once

#include <vector>
#include "SceneObject.h"
#include "Model\DMModel.h"
#include "Common\DMTransform.h"
#include "ObjectLibrary\LevelDescription.h"

namespace GS
{

// Модели уровня (таблица LevelModels): у каждого экземпляра свой трансформ, модель — общий ресурс. За каждый вид
// выбирает LOD экземпляра по расстоянию от точки LOD вида и отдаёт его меш (MeshBatch); в какой проход он попадёт
// (непрозрачные, полупрозрачные) и в каком порядке будет нарисован, решает рендерер
class ModelInstances : public SceneObject
{
public:
	ModelInstances();

	// Вызывается после загрузки моделей: экземпляры уровня и свойства их моделей для GUI
	void initialize( const std::vector<LevelDescription::ModelInstance>& instances );

	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	PropertyContainer* properties() override;

private:
	struct Instance
	{
		DMModel* model;
		DMTransform transform;
	};

	std::vector<Instance> m_instances;
	PropertyContainer m_properties;
};

}
