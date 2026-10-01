#pragma once
//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <vector>
#include <memory>
#include <unordered_set>

#include "..\Texture\DMTextureStorage.h"
#include "Mesh\DMMesh.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

// Модель — ресурс: LOD из секций и дальности переключения. Где стоит модель, задают её экземпляры (ModelInstances,
// SkySphere): одна модель может стоять на уровне в нескольких местах
class DMModel : public DMResource
{
public:
	// Секция LOD — меш со своим материалом, как section / material slot у Static Mesh в UE и primitive у меша glTF:
	// у дерева непрозрачный ствол и Masked хвоя, у постройки несколько материалов. Строка ModelProperties
	struct Section
	{
		uint32_t mesh;
		uint32_t material;
		PropertyContainer params;	// параметры материала; адрес постоянен — на него ссылаются MeshBatch, GUI, расстановка
	};

	struct LodBlock
	{
		std::vector<std::unique_ptr<Section>> sections;
		bool isRender = true;
		DirectX::BoundingBox bounds;	// объединение границ мешей секций, в пространстве модели
	};
public:
	DMModel( uint32_t id, const std::string& name = "" );
	DMModel( DMModel&& );
	DMModel& operator=( DMModel&& );
	~DMModel();

	void addLod( float range, std::unique_ptr<LodBlock>&& lod );
	// LOD для расстояния до камеры: первый, чья дальность не меньше; дальше последнего — nullptr
	const LodBlock* getLod( float distance ) const;
	// Номер LOD для расстояния или −1, если дальше последнего
	int lodIndex( float distance ) const;
	LodBlock* getLodById( uint16_t index );
	// Дальность LOD index, м: до неё включительно рисуется этот LOD (ModelProperties.range)
	float lodRange( uint16_t index ) const;
	uint16_t lodCount();

	PropertyContainer* properties();
private:
	std::vector<std::pair<float, std::shared_ptr<LodBlock>>> m_lods;
	PropertyContainer m_properties;
};

}