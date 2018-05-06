#pragma once

//////////////
// INCLUDES //
//////////////
#include "DMShader.h"

namespace GS
{

class DMPhongLight : public DMShader
{
public:


public:
	DMPhongLight();
	~DMPhongLight();
	bool Prepare();
	bool setTexture( uint32_t albedo, uint32_t normal );

private:
	bool innerInitialize() override;
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

};

}