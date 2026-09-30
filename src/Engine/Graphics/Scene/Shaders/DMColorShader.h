#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>
#include "DMShader.h"

namespace GS
{

class DMColorShader : public DMShader
{
public:
	DMColorShader();
	~DMColorShader();
	void setParams( const PropertyContainer& ) override;

private:
	bool innerInitialize() override;
	std::vector<VertexElement> initLayouts() override;
	Buffer m_constantBuffer;
};

}
