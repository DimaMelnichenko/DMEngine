#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "Properties\PropertyContainer.h"

namespace GS
{

// Экземпляры материалов (строки MaterialInstance) — как Material Instance в UE: набор значений параметров материала, один
// на все секции моделей, которые на него ссылаются (ModelProperties.material_instance_id). У одуванчика — пять моделей
// по три LOD с одним экземпляром: правка в окне видна у всех сразу, «Save level» пишет изменённое в
// MaterialParameterInstance (LibraryLoader::saveMaterialInstances). Создаёт и наполняет LibraryLoader при загрузке
// моделей; адреса параметров постоянны — на них ссылаются секции, MeshBatch, расстановка и GUI
class MaterialInstanceStorage
{
public:
	struct Instance
	{
		uint32_t id = 0;
		uint32_t material = 0;		// Materials.id: чьи определения параметров (MaterialParameterDef)
		PropertyContainer params;	// имя — MaterialInstance.name
	};

	// Экземпляр id или nullptr, если его ещё не загружали
	Instance* find( uint32_t id )
	{
		const auto it = m_byId.find( id );
		return it != m_byId.end() ? it->second : nullptr;
	}
	Instance& add( uint32_t id, uint32_t material, PropertyContainer&& params )
	{
		m_instances.push_back( std::make_unique<Instance>( Instance{ id, material, std::move( params ) } ) );
		m_byId[id] = m_instances.back().get();
		return *m_instances.back();
	}
	const std::vector<std::unique_ptr<Instance>>& all() const { return m_instances; }

private:
	std::vector<std::unique_ptr<Instance>> m_instances;
	std::unordered_map<uint32_t, Instance*> m_byId;
};

}
