#pragma once

#include <unordered_map>
#include "Storage\DMResourceStorage.h"
#include "MaterialLoader.h"

namespace GS
{

class MaterialStorage : public DMResourceStorage<std::unique_ptr<Material>>
{
public:
	MaterialStorage( const std::string& path );
	~MaterialStorage();

	bool createMaterial( uint32_t id, const std::string& name, const std::string& matClass );

private:
	MaterialLoader m_materialLoader;
};

}