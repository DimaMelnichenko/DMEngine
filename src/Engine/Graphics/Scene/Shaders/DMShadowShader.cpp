#include "DMShadowShader.h"


DMShadowShader::DMShadowShader( DMD3D* parent ) : DMShader( parent )
{
	
}


DMShadowShader::~DMShadowShader()
{

}

void DMShadowShader::prepareRender()
{

}

void DMShadowShader::Update( float )
{

}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMShadowShader::initLayouts()
{	
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

	polygonLayout.SemanticName = "TEXCOORD";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;


	return vertex_layout;
}