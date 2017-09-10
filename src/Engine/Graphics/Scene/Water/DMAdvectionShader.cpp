#include "DMAdvectionShader.h"



DMAdvectionShader::DMAdvectionShader( DMD3D* parent ) : DMShader( parent )
{
}

DMAdvectionShader::~DMAdvectionShader()
{
	Shutdown();
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMAdvectionShader::initLayouts()
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

	// Create a texture sampler state description.
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
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
	ID3D11SamplerState* sampler_state;
	result = m_dmd3d->GetDevice()->CreateSamplerState( &samplerDesc, &sampler_state );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	m_texture_sampler = std::move( std::shared_ptr<ID3D11SamplerState>( sampler_state, delete_com ) );

	D3D11_BUFFER_DESC param_buffer_desc;
	param_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	param_buffer_desc.ByteWidth = sizeof( PSParamBuffer );
	param_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	param_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	param_buffer_desc.MiscFlags = 0;
	param_buffer_desc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	ID3D11Buffer* buffer;
	result = m_dmd3d->GetDevice()->CreateBuffer( &param_buffer_desc, NULL, &buffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	m_param_buffer = std::move( std::shared_ptr<ID3D11Buffer>( buffer, delete_com ) );

	return vertex_layout;
}

void DMAdvectionShader::setParameters( PSParamBuffer* params )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	ID3D11Buffer* buffer = m_param_buffer.get();

	HRESULT result = m_dmd3d->GetDeviceContext()->Map( buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	PSParamBuffer* dataPtr = static_cast<PSParamBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( dataPtr, params, sizeof( PSParamBuffer ) );

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( buffer, 0 );

	
	m_dmd3d->GetDeviceContext()->PSSetConstantBuffers( 1, 1, &buffer );
}

void DMAdvectionShader::Shutdown()
{

}

void DMAdvectionShader::prepareRender()
{
	ID3D11SamplerState* sampler = m_texture_sampler.get();
	m_dmd3d->GetDeviceContext()->PSSetSamplers( 0, 1, &sampler );
}

void DMAdvectionShader::Update( float )
{

}

void DMAdvectionShader::setTexures( int count, ID3D11ShaderResourceView** texture )
{
	// Set shader texture resource in the pixel shader.
	m_dmd3d->GetDeviceContext()->PSSetShaderResources( 0, count, texture );
	
}