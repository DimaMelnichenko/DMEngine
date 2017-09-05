#include "DMTerrainShader.h"


DMTerrainShader::DMTerrainShader( DMD3D* parent ) : DMShader( parent )
{	
	m_sampleState = 0;
	m_lightBuffer = 0;	
	m_topologyBuffer = 0;
	m_terrainBuffer = 0;
	m_textureBuffer = 0;
	m_current_tex_offset = 0;
	m_tex_offset_speed = 0;
	m_tex_repeat = 0;
	m_sum_time = 0;
}

DMTerrainShader::~DMTerrainShader()
{
	Shutdown();
}

void DMTerrainShader::Shutdown( )
{
	// Shutdown the vertex and pixel shaders as well as the related objects.
	ShutdownShader( );

	return;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMTerrainShader::initLayouts()
{
	HRESULT result;	
	D3D11_INPUT_ELEMENT_DESC polygonLayout;

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	D3D11_SAMPLER_DESC samplerDesc;
	D3D11_BUFFER_DESC lightBufferDesc;
	D3D11_BUFFER_DESC bufferDesc;

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

	// Create a texture sampler state description.
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;// D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.BorderColor[0] = 0;
	samplerDesc.BorderColor[1] = 0;
	samplerDesc.BorderColor[2] = 0;
	samplerDesc.BorderColor[3] = 0;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	// Create the texture sampler state.
	result = m_dmd3d->GetDevice()->CreateSamplerState( &samplerDesc, &m_sampleHM );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	// Create a texture sampler state description.
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
	result = m_dmd3d->GetDevice()->CreateSamplerState( &samplerDesc, &m_sampleState );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	// Setup the description of the light dynamic constant buffer that is in the pixel shader.
	// Note that ByteWidth always needs to be a multiple of 16 if using D3D11_BIND_CONSTANT_BUFFER or CreateBuffer will fail.
	lightBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	lightBufferDesc.ByteWidth = sizeof( LightBufferType );
	lightBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	lightBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	lightBufferDesc.MiscFlags = 0;
	lightBufferDesc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = m_dmd3d->GetDevice()->CreateBuffer( &lightBufferDesc, NULL, &m_lightBuffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	// Setup the description of the light dynamic constant buffer that is in the pixel shader.
	// Note that ByteWidth always needs to be a multiple of 16 if using D3D11_BIND_CONSTANT_BUFFER or CreateBuffer will fail.
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = sizeof( TerrainType );
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = m_dmd3d->GetDevice()->CreateBuffer( &bufferDesc, NULL, &m_terrainBuffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = sizeof( TopologyType );
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = m_dmd3d->GetDevice()->CreateBuffer( &bufferDesc, NULL, &m_topologyBuffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = sizeof( TexParamType );
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = m_dmd3d->GetDevice()->CreateBuffer( &bufferDesc, NULL, &m_textureBuffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	return vertex_layout;
}

void DMTerrainShader::ShutdownShader( )
{

	if( m_textureBuffer )
	{
		m_textureBuffer->Release();
		m_textureBuffer = 0;
	}

	if( m_topologyBuffer )
	{
		m_topologyBuffer->Release();
		m_topologyBuffer = NULL;
	}

	// Release the light constant buffer.
	if( m_lightBuffer )
	{
		m_lightBuffer->Release( );
		m_lightBuffer = 0;
	}

	// Release the sampler state.
	if( m_sampleState )
	{
		m_sampleState->Release( );
		m_sampleState = 0;
	}

	return;
}

bool DMTerrainShader::SetParameters( ID3D11DeviceContext* deviceContext, DMLight* light, float _level, ID3D11ShaderResourceView** textures, int _num_resource )
{
	HRESULT result;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	LightBufferType* dataPtr;
	TopologyType *topologyType;
	TexParamType* texture_type;
	


	// Lock the light constant buffer so it can be written to.
	result = m_dmd3d->GetDeviceContext()->Map( m_lightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	dataPtr = (LightBufferType*)mappedResource.pData;

	// Copy the lighting variables into the constant buffer.
	dataPtr->lightDirection = light->direction();
	dataPtr->padding = 2.0f;

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_lightBuffer, 0 );

	// Finally set the light constant buffer in the pixel shader with the updated values.
	m_dmd3d->GetDeviceContext()->PSSetConstantBuffers( 0, 1, &m_lightBuffer );


	// Lock the light constant buffer so it can be written to.
	result = m_dmd3d->GetDeviceContext()->Map( m_textureBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	texture_type = ( TexParamType* )mappedResource.pData;

	// Copy the lighting variables into the constant buffer.
	texture_type->param_set.x = m_tex_repeat;
	texture_type->param_set.y = m_current_tex_offset;
	texture_type->param_set.w = m_elapsedTime;


	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_textureBuffer, 0 );


	// Finally set the light constant buffer in the pixel shader with the updated values.
	m_dmd3d->GetDeviceContext()->PSSetConstantBuffers( 1, 1, &m_textureBuffer );



	// Lock the light constant buffer so it can be written to.
	result = m_dmd3d->GetDeviceContext()->Map( m_topologyBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	topologyType = ( TopologyType* )mappedResource.pData;

	topologyType->param_set.x = _level;

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_topologyBuffer, 0 );
	

	// Finally set the light constant buffer in the pixel shader with the updated values.
	m_dmd3d->GetDeviceContext()->VSSetConstantBuffers( 2, 1, &m_topologyBuffer );

	// Set shader texture resources in the pixel shader.
	m_dmd3d->GetDeviceContext()->VSSetShaderResources( 0, _num_resource, textures );
	m_dmd3d->GetDeviceContext()->PSSetShaderResources( 0, _num_resource, textures );

	return true;
}

bool DMTerrainShader::SetTextureParams( ID3D11DeviceContext* _deviceContext, float _repeat, float _speed_translate )
{	
	m_tex_offset_speed = _speed_translate;	

	m_tex_repeat = _repeat;
	
	return true;
}

bool DMTerrainShader::setTextureCoordOffset( ID3D11DeviceContext* deviceContext, float start_x, float start_y, float end_x, float end_y, D3DXVECTOR4 _terrain_params, D3DXVECTOR4* neighbors )
{
	HRESULT result;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	TerrainType* terrainData;

	// Lock the light constant buffer so it can be written to.
	result = m_dmd3d->GetDeviceContext()->Map( m_terrainBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	terrainData = (TerrainType*)mappedResource.pData;

	// Copy the lighting variables into the constant buffer.
	terrainData->texOffset.x = start_x;
	terrainData->texOffset.y = start_y;
	terrainData->texOffset.z = end_x;
	terrainData->texOffset.w = end_y;
	terrainData->scale = _terrain_params;
	terrainData->scale.z = m_sum_time;
	terrainData->neighbors = D3DXVECTOR4( neighbors->x, 0.0, 0.0, 0.0 );
	

	// Unlock the constant buffer.
	m_dmd3d->GetDeviceContext()->Unmap( m_terrainBuffer, 0 );

	m_dmd3d->GetDeviceContext()->VSSetConstantBuffers( 1, 1, &m_terrainBuffer );
	m_dmd3d->GetDeviceContext()->PSSetConstantBuffers( 2, 1, &m_terrainBuffer );

	return true;
}

void DMTerrainShader::prepareRender()
{
	// Set the sampler state in the pixel shader.
	m_dmd3d->GetDeviceContext()->VSSetSamplers( 0, 1, &m_sampleHM );
	m_dmd3d->GetDeviceContext()->VSSetSamplers( 1, 1, &m_sampleState );
	m_dmd3d->GetDeviceContext()->PSSetSamplers( 0, 1, &m_sampleState );

	return;
}

void DMTerrainShader::Update( float _elapsed_time )
{
	m_current_tex_offset += m_tex_offset_speed * _elapsed_time;

	if( m_current_tex_offset > 1.0f )
	{
		m_current_tex_offset = m_current_tex_offset - 1.0f;
	}

	m_elapsedTime = _elapsed_time;

	m_sum_time += m_elapsedTime;
}