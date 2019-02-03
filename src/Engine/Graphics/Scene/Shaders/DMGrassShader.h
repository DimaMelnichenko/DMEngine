#pragma once

#include "Shaders\DMShader.h"

namespace GS
{

class DMGrassShader : public DMShader
{
public:
	DMGrassShader();
	~DMGrassShader();
private:
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();
	bool innerInitialize() override;

private:
};

}