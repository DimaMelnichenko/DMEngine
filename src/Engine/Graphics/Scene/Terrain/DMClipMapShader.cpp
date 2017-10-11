#include "DMClipMapShader.h"


DMClipMapShader::DMClipMapShader()
{	
	m_max_MxM_instance_count = 120;
}


DMClipMapShader::~DMClipMapShader()
{
	
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMClipMapShader::initLayouts()
{
	HRESULT result;
	D3D11_INPUT_ELEMENT_DESC polygonLayout;

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

//	D3D11_SAMPLER_DESC samplerDesc;

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
/*
	polygonLayout.SemanticName = "TEXCOORD";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );
	*/
	D3D11_BUFFER_DESC param_buffer_desc;
	param_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	param_buffer_desc.ByteWidth = sizeof( ParamBuffer );
	param_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	param_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	param_buffer_desc.MiscFlags = 0;
	param_buffer_desc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	ID3D11Buffer* buffer;
	result = DMD3D::instance().GetDevice()->CreateBuffer( &param_buffer_desc, NULL, &buffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	m_shader_param = make_com_ptr<ID3D11Buffer>( buffer );

	param_buffer_desc.ByteWidth = sizeof( PSParamBuffer );
	result = DMD3D::instance().GetDevice()->CreateBuffer( &param_buffer_desc, NULL, &buffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	m_ps_shader_param = make_com_ptr<ID3D11Buffer>( buffer );

	//// create structured buffer
	param_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	param_buffer_desc.ByteWidth = sizeof( InstanceOffset ) * m_max_MxM_instance_count;
	param_buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	param_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	param_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	param_buffer_desc.StructureByteStride = sizeof( InstanceOffset );

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.	
	result = DMD3D::instance().GetDevice()->CreateBuffer( &param_buffer_desc, NULL, &buffer );
	if( FAILED( result ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	m_offset_sbuffer = make_com_ptr<ID3D11Buffer>( buffer );

	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
	std::memset( &viewDesc, 0, sizeof( viewDesc ) );

	viewDesc.Format = DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	viewDesc.Buffer.ElementWidth = m_max_MxM_instance_count;

	ID3D11ShaderResourceView* srv;
	if( FAILED( DMD3D::instance().GetDevice()->CreateShaderResourceView( m_offset_sbuffer.get(), &viewDesc, &srv ) ) )
	{
		return std::vector<D3D11_INPUT_ELEMENT_DESC>();
	}

	m_srv_buffer = make_com_ptr<ID3D11ShaderResourceView>( srv );

	return vertex_layout;
}

bool DMClipMapShader::setInstanceOffset( std::vector<InstanceOffset>& values )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	ID3D11Buffer* buffer = m_offset_sbuffer.get();

	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	InstanceOffset* data_ps = static_cast<InstanceOffset*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( data_ps, &values[0], sizeof( InstanceOffset ) * min( values.size(), m_max_MxM_instance_count ) );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( buffer, 0 );


	ID3D11ShaderResourceView* srv = m_srv_buffer.get();
	DMD3D::instance().GetDeviceContext()->VSSetShaderResources( 16, 1, &srv );

	return true;
}

bool DMClipMapShader::SetPSParameters( PSParamBuffer* ps_param )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	ID3D11Buffer* buffer = m_ps_shader_param.get();

	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	PSParamBuffer* data_ps = static_cast<PSParamBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( data_ps, ps_param, sizeof( PSParamBuffer ) );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( buffer, 0 );


	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 1, 1, &buffer );

	return true;
}

bool DMClipMapShader::SetVSParameters( ParamBuffer* _param_buffer )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	ID3D11Buffer* buffer = m_shader_param.get();
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	ParamBuffer* dataPtr = static_cast<ParamBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( dataPtr, _param_buffer, sizeof( ParamBuffer ) );	

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( buffer, 0 );

	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 2, 1, &buffer );

	return true;
}

void DMClipMapShader::prepareRender( )
{

	

	return;
}

void DMClipMapShader::Update( float )
{

}

void DMClipMapShader::SetTextures( int count, ID3D11ShaderResourceView** _textures )
{
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 10, count, _textures );
}