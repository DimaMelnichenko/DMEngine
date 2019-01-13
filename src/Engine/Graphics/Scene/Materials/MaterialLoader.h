#pragma once
#include "Material.h"

namespace GS
{

class MaterialLoader
{
public:
	MaterialLoader();
	~MaterialLoader();

	Material* createMaterial( uint32_t id, const std::string& name, const std::string& matClass );
};

}