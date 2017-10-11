#include "DMDepthShader.h"


DMDepthShader::DMDepthShader()
{
	
}


DMDepthShader::~DMDepthShader()
{

}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMDepthShader::initLayouts()
{
	HRESULT result;
	D3D11_INPUT_ELEMENT_DESC polygonLayout;
	
	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	// Create the vertex input layout description.
	// This setup needs to match the VertexType stucture in the ModelClass and in the shader.
	polygonLayout.SemanticName = "POSITION";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = 0;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	return vertex_layout;
}

void DMDepthShader::Update( float )
{

}

void DMDepthShader::prepareRender()
{

}