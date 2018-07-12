#pragma once
#include "D3D/DMD3D.h"
#include <D3DX11tex.h>
#include "Material.h"

namespace GS
{

class MaterialLoader
{
public:
	MaterialLoader();
	~MaterialLoader();

	Material* loadFromFile( uint32_t id, const std::string& name, const std::string& vsShaderName, const std::string& gsShaderName, const std::string& psShaderName );
};

}