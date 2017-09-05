#include "DMOceanShader.h"


DMOceanShader::DMOceanShader( DMD3D* _parent ) : DMShader( _parent )
{
	m_vs_shader_param = nullptr;
	m_ps_shader_param = nullptr;
	m_samplerStateColor = nullptr;
}


DMOceanShader::~DMOceanShader( )
{
	Shutdown( );
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMOceanShader::initLayouts()
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

	polygonLayout.SemanticName = "TEXCOORD";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	D3D11_BUFFER_DESC param_buffer_desc;
	param_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	param_buffer_desc.ByteWidth = sizeof( VSParamBuffer );
	param_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	param_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	param_buffer_desc.MiscFlags = 0;
	param_buffer_desc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = m_dmd3d->GetDevice()->CreateBuffer( &param_buffer_desc, NULL, &m_vs_shader_param );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	param_buffer_desc.ByteWidth = sizeof( PSParamBuffer );
	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = m_dmd3d->GetDevice()->CreateBuffer( &param_buffer_desc, NULL, &m_ps_shader_param );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	D3D11_SAMPLER_DESC samplerDesc;
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.BorderColor[0] = 0;
	samplerDesc.BorderColor[1] = 0;
	samplerDesc.BorderColor[2] = 0;
	samplerDesc.BorderColor[3] = 0;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	// Create the texture sampler state.
	result = m_dmd3d->GetDevice()->CreateSamplerState( &samplerDesc, &m_samplerStateColor );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	return vertex_layout;
}

void DMOceanShader::Shutdown( )
{
	// Release the sampler state.
	if( m_vs_shader_param )
	{
		m_vs_shader_param->Release( );
		m_vs_shader_param = nullptr;
	}	  

	if( m_ps_shader_param )
	{
		m_ps_shader_param->Release( );
		m_ps_shader_param = nullptr;
	}

	if( m_samplerStateColor )
	{
		m_samplerStateColor->Release( );
		m_samplerStateColor = nullptr;
	}

	return;
}

bool DMOceanShader::SetParameters( VSParamBuffer* _vs_param_buffer, PSParamBuffer* _ps_param_buffer )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	HRESULT result = m_dmd3d->GetDeviceContext()->Map( m_vs_shader_param, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	VSParamBuffer* vs_dataPtr = static_cast<VSParamBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( vs_dataPtr, _vs_param_buffer, sizeof( VSParamBuffer ) );

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_vs_shader_param, 0 );


	result = m_dmd3d->GetDeviceContext()->Map( m_ps_shader_param, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	PSParamBuffer* ps_dataPtr = static_cast<PSParamBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( ps_dataPtr, _ps_param_buffer, sizeof( PSParamBuffer ) );

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_ps_shader_param, 0 );

	m_dmd3d->GetDeviceContext()->VSSetConstantBuffers( 1, 1, &m_vs_shader_param );
	m_dmd3d->GetDeviceContext()->PSSetConstantBuffers( 0, 1, &m_ps_shader_param );

	return true;
}

void DMOceanShader::prepareRender()
{

	// Set the sampler state in the pixel shader.
	m_dmd3d->GetDeviceContext()->VSSetSamplers( 0, 1, &m_samplerStateColor );
	m_dmd3d->GetDeviceContext()->PSSetSamplers( 0, 1, &m_samplerStateColor );

	return;
}

void DMOceanShader::Update( float )
{

}

void DMOceanShader::SetTextures( int count, ID3D11ShaderResourceView** _textures )
{
	m_dmd3d->GetDeviceContext()->VSSetShaderResources( 0, count, _textures );
	m_dmd3d->GetDeviceContext()->PSSetShaderResources( 0, count, _textures );
}