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

	bool load( const std::string& file );

private:
	MaterialLoader m_materialLoader;
};

}