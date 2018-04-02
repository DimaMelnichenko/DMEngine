#pragma once

#include <unordered_map>
#include "DMResourceStorage.h"
#include "Material.h"

namespace GS
{

class MaterialStorage : public DMResourceStorage<Material>
{
public:
	MaterialStorage( const std::wstring& path );
	~MaterialStorage();

	bool load( const std::wstring& file );

private:
	//ModelLoader m_modelLoader;
};

}