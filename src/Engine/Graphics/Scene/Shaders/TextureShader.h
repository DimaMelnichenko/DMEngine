#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>
#include "DMShader.h"

namespace GS
{

class TextureShader : public DMShader
{
public:
	TextureShader();
	~TextureShader();
	void setParams( const PropertyContainer& ) override;

private:
	bool innerInitialize() override;
	std::vector<VertexElement> initLayouts() override;
};

}
