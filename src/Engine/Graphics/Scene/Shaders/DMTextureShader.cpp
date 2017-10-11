#include "DMTextureShader.h"


DMTextureShader::DMTextureShader()
{	

}

DMTextureShader::~DMTextureShader()
{

}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMTextureShader::initLayouts(  )
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

	polygonLayout.SemanticName = "POSITION";
	polygonLayout.SemanticIndex = 1;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	polygonLayout.InputSlot = 1;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
	polygonLayout.InstanceDataStepRate = 0;
	
	vertex_layout.push_back( polygonLayout );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( PSParamBuffer ), m_param_buffer ) )
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();

	return vertex_layout;
}

bool DMTextureShader::setTexure( ID3D11ShaderResourceView* texture )
{
	// Set shader texture resource in the pixel shader.	
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &texture );

	return true;
}

bool DMTextureShader::setParameters( PSParamBuffer* param_buffer )
{
	// Set shader texture resource in the pixel shader.
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( m_param_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	PSParamBuffer* dataPtr = static_cast<PSParamBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( dataPtr, param_buffer, sizeof( PSParamBuffer ) );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( m_param_buffer.get(), 0 );
	

	return true;
}

void DMTextureShader::prepareRender( )
{	
	ID3D11Buffer* buffer = m_param_buffer.get();
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 1, 1, &buffer );

	return;
}

void DMTextureShader::Update( float )
{

}