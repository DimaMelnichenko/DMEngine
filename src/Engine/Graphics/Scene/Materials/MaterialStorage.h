#pragma once

#include <unordered_map>
#include "DMResourceStorage.h"
#include "MaterialLoader.h"

namespace GS
{

class MaterialStorage : public DMResourceStorage<std::unique_ptr<Material>>
{
public:
	MaterialStorage( const std::string& path );
	~MaterialStorage();

	bool load( uint32_t id, const std::string& name, const std::string& vs, const std::string& gs, const std::string& ps );

private:
	MaterialLoader m_materialLoader;
};

}