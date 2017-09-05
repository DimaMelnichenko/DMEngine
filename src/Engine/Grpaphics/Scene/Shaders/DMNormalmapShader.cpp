#include "DMNormalmapShader.h"


DMNormalmapShader::DMNormalmapShader( DMD3D* parent ) : DMShader( parent )
{
	m_sampleState = 0;
	m_param_buffer = nullptr;
}

DMNormalmapShader::~DMNormalmapShader( )
{
	Shutdown( );
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMNormalmapShader::initLayouts()
{
	HRESULT result;
	D3D11_INPUT_ELEMENT_DESC polygonLayout;
	
	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	D3D11_SAMPLER_DESC samplerDesc;

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

	polygonLayout.SemanticName = "TEXCOORD";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "NORMAL";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "TANGENT";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "BINORMAL";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	// Create a texture sampler state description.
	//samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 0;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.BorderColor[0] = 0;
	samplerDesc.BorderColor[1] = 0;
	samplerDesc.BorderColor[2] = 0;
	samplerDesc.BorderColor[3] = 0;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	// Create the texture sampler state.
	result = m_dmd3d->GetDevice()->CreateSamplerState( &samplerDesc, &m_sampleState );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	D3D11_BUFFER_DESC param_buffer_desc;
	param_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	param_buffer_desc.ByteWidth = sizeof( PSParamBuffer );
	param_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	param_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	param_buffer_desc.MiscFlags = 0;
	param_buffer_desc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = m_dmd3d->GetDevice()->CreateBuffer( &param_buffer_desc, NULL, &m_param_buffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	return vertex_layout;
}

void DMNormalmapShader::Shutdown( )
{
	// Release the sampler state.
	if( m_sampleState )
	{
		m_sampleState->Release( );
		m_sampleState = 0;
	}

	if( m_param_buffer )
	{
		m_param_buffer->Release( );
		m_param_buffer = nullptr;
	}

	return;
}

bool DMNormalmapShader::setTexure( ID3D11ShaderResourceView** texture )
{
	// Set shader texture resource in the pixel shader.
	m_dmd3d->GetDeviceContext()->PSSetShaderResources( 0, 2, texture );

	return true;
}

bool DMNormalmapShader::setParameters( PSParamBuffer* _param_buffer )
{
	// Set shader texture resource in the pixel shader.
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	HRESULT result = m_dmd3d->GetDeviceContext()->Map( m_param_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	PSParamBuffer* dataPtr = static_cast<PSParamBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( dataPtr, _param_buffer, sizeof( PSParamBuffer ) );

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_param_buffer, 0 );


	return true;
}

void DMNormalmapShader::prepareRender( )
{

	m_dmd3d->GetDeviceContext()->PSSetConstantBuffers( 1, 1, &m_param_buffer );

	// Set the sampler state in the pixel shader.
	m_dmd3d->GetDeviceContext()->PSSetSamplers( 0, 1, &m_sampleState );

	return;
}

void DMNormalmapShader::Update( float )
{

}