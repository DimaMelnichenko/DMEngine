#include "DMColorShader.h"

namespace GS
{

DMColorShader::DMColorShader()
{

}

DMColorShader::~DMColorShader()
{
	
}


bool DMColorShader::innerInitialize()
{
	ID3D11Buffer* buffer;
	DMD3D::instance().createShaderConstantBuffer( sizeof( D3DXVECTOR4 ), m_constantBuffer, nullptr );

	createPhase( 0, -1, 0 );
	return true;
}

bool DMColorShader::Prepare()
{
	return DMShader::setPass( 0 );
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMColorShader::initLayouts()
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

void DMColorShader::setParams( const ParamSet& params )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( m_constantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );

	memcpy( mappedResource.pData, &params.at("Color").vector() , sizeof( D3DXVECTOR4 ) );

	DMD3D::instance().GetDeviceContext()->Unmap( m_constantBuffer.get(), 0 );
	ID3D11Buffer* buffer = m_constantBuffer.get();
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 2, 1, &buffer );
}

}