#pragma once

//////////////
// INCLUDES //
//////////////
#include "DMShader.h"

namespace GS
{

class VertexLight : public DMShader
{
public:


public:
	VertexLight();
	~VertexLight();
	bool Prepare();
	void setParams( const ParamSet& ) override;

private:
	bool innerInitialize() override;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

};

}