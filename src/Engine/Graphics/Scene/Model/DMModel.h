#pragma once
//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <vector>
#include <memory>
#include <unordered_set>

#include "..\TextureObjects\DMTextureStorage.h"
#include "Mesh\DMMesh.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

// Модель — ресурс: LOD (меш, материал, параметры материала) и дальности переключения. Где стоит модель, задают
// её экземпляры (ModelInstances, SkySphere): одна модель может стоять на уровне в нескольких местах
class DMModel : public DMResource
{
public:
	struct LodBlock
	{
		uint32_t mesh;
		uint32_t material;
		PropertyContainer params;
		bool isRender;
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
	uint16_t lodCount();

	PropertyContainer* properties();
private:
	std::vector<std::pair<float, std::shared_ptr<LodBlock>>> m_lods;
	PropertyContainer m_properties;
};

}