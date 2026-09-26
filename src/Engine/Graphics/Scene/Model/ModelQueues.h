#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "SceneObject.h"
#include "Model\DMModel.h"
#include "Common\DMTransform.h"
#include "ObjectLibrary\LevelDescription.h"

namespace GS
{

// Модели уровня (таблица LevelModels): у каждого экземпляра свой трансформ, модель — общий ресурс.
// В update() выбирает LOD экземпляра по расстоянию до камеры и раскладывает по режиму материала: непрозрачные
// и вырезанные по альфе — в очереди по материалу (проход opaque), полупрозрачные — в список от дальних к ближним
// (проход transparent). Двусторонние рисуются без отсечения граней
class ModelQueues : public SceneObject
{
public:
	ModelQueues();

	// Вызывается после загрузки моделей: экземпляры уровня и свойства их моделей для GUI
	void initialize( const std::vector<LevelDescription::ModelInstance>& instances );

	void update( const FrameContext& frame ) override;
	bool drawsIn( RenderPass pass ) const override;
	void render( const FrameContext& frame, RenderPass pass ) override;
	PropertyContainer* properties() override;

private:
	struct Instance
	{
		DMModel* model;
		DMTransform transform;
	};

	struct DrawItem
	{
		const DMModel::LodBlock* lod;
		const DMTransform* transform;
		MaterialRenderState state;
		float distance;
	};

	void draw( const DrawItem& item, RasterState frameRaster ) const;

	std::vector<Instance> m_instances;
	std::unordered_map<uint32_t, std::vector<DrawItem>> m_renderQueues;	// материал → что им рисовать
	std::vector<DrawItem> m_translucent;	// от дальних к ближним
	PropertyContainer m_properties;
};

}
