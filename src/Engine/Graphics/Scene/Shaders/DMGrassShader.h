#pragma once

#include "Shaders\DMShader.h"

namespace GS
{

class DMGrassShader : public DMShader
{
public:
	DMGrassShader();
	~DMGrassShader();

	void setParams( const ParamSet& ) override;

private:
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts() override;
	bool innerInitialize() override;

private:
};

}