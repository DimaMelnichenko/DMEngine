#pragma once

#include "Material.h"
#include "Shaders\color_material_constants.h"

namespace GS
{

// Материал без освещения одним цветом (параметр Color): небо, отладка
class ColorMaterial : public Material
{
public:
	ColorMaterial( uint32_t id, const std::string& name );
	bool initialize() override;
	std::vector<VertexElement> initLayouts() override;
	void setParams( const PropertyContainer& ) override;

private:
	Buffer m_constantBuffer;
};

}
