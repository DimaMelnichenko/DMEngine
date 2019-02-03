#include "DMGrassShader.h"

namespace GS
{

DMGrassShader::DMGrassShader()
{

}

DMGrassShader::~DMGrassShader()
{

}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMGrassShader::initLayouts()
{
	return std::vector<D3D11_INPUT_ELEMENT_DESC>();
}

bool DMGrassShader::innerInitialize()
{
	return true;
}

}