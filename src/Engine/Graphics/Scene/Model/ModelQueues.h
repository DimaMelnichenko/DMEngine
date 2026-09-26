#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "SceneObject.h"
#include "Model\DMModel.h"

namespace GS
{

// Модели из хранилища System::models(): в update() выбирает LOD по расстоянию до камеры
// и раскладывает их в очереди по материалу, в render() рисует очереди
class ModelQueues : public SceneObject
{
public:
	ModelQueues();

	// Вызывается после загрузки моделей: собирает их свойства для GUI
	void initialize();
	// Модель, которую рисует другой объект сцены (например, SkySphere)
	void exclude( const std::string& modelName );

	void update( const FrameContext& frame ) override;
	void render( const FrameContext& frame ) override;
	PropertyContainer* properties() override;

private:
	using RenderQueue = std::vector<const DMModel::LodBlock*>;
	std::unordered_map<uint32_t, RenderQueue> m_renderQueues;
	std::unordered_set<std::string> m_excluded;
	PropertyContainer m_properties;
};

}
