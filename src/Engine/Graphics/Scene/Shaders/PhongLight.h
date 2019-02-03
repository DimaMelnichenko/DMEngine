#pragma once

//////////////
// INCLUDES //
//////////////
#include "DMShader.h"

namespace GS
{

class PhongLight : public DMShader
{
public:


public:
	PhongLight();
	~PhongLight();
	bool Prepare();
	void setParams( const PropertyContainer& ) override;

private:
	bool innerInitialize() override;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

};

}