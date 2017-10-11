#include "DMFullScreenShader.h"



DMFullScreenShader::DMFullScreenShader()
{
}


DMFullScreenShader::~DMFullScreenShader()
{

}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMFullScreenShader::initLayouts()
{
	return std::vector<D3D11_INPUT_ELEMENT_DESC>();
}

void DMFullScreenShader::prepareRender()
{
	setDrawType( by_vertex );
}

void DMFullScreenShader::Update( float )
{

}