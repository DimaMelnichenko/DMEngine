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

	Material* loadFromFile( const std::string& filPath, uint32_t id, const std::string& name );
};

}