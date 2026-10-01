#pragma once

#include "Material.h"

namespace GS
{

// Материал без освещения с текстурой (параметр Albedo, t0): небо, отладка
class TextureMaterial : public Material
{
public:
	TextureMaterial( uint32_t id, const std::string& name );
	bool initialize() override;
	std::vector<VertexElement> initLayouts() override;
	void setParams( const PropertyContainer& ) override;
};

}
