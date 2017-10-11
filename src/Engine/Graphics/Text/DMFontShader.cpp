#include "DMFontShader.h"


DMFontShader::DMFontShader()
{
	
}

DMFontShader::~DMFontShader()
{

}

void DMFontShader::Update( float )
{

}

bool DMFontShader::setColor( D3DXVECTOR4 color )
{
	// Set shader texture resource in the pixel shader.
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( m_param_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	ParameterType* dataPtr = static_cast<ParameterType*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( dataPtr, color, sizeof( ParameterType ) );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( m_param_buffer.get(), 0 );

	return true;
}

bool DMFontShader::setTexure( ID3D11ShaderResourceView* texture )
{	
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &texture );

	return true;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMFontShader::initLayouts()
{
	D3D11_INPUT_ELEMENT_DESC polygonLayout;

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;


	// Create the vertex input layout description.	
	polygonLayout.SemanticName = "POSITION";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = 0;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "TEXCOORD";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ParameterType ), m_param_buffer ) )
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();

	return vertex_layout;
}

void DMFontShader::prepareRender()
{
	ID3D11Buffer* buffer = m_param_buffer.get();
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 1, 1, &buffer );

	return;
}