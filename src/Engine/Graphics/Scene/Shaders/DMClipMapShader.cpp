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


	m_blockOffsets.createBuffer( sizeof( InstanceOffset ), m_max_MxM_instance_count );
	m_materials.createBuffer( sizeof( Material ), 20 );

	DMD3D::instance().createShaderConstantBuffer( sizeof( ParamBuffer ), m_shader_param );

	DMD3D::instance().createShaderConstantBuffer( sizeof( PSParamBuffer ), m_ps_shader_param );

	return vertex_layout;
}

bool DMClipMapShader::setInstanceOffset( std::vector<InstanceOffset>& values )
{
	m_blockOffsets.updateData( &values[0], sizeof( InstanceOffset ) * values.size() );

	m_blockOffsets.setToSlot( 1, SRVType::vs );

	return true;
}

bool DMClipMapShader::SetPSParameters( PSParamBuffer* ps_param )
{
	ID3D11Buffer* buffer = m_ps_shader_param.get();

	Device::updateResourceData<PSParamBuffer>( buffer, *ps_param );

	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 3, 1, &buffer );

	return true;
}

bool DMClipMapShader::SetVSParameters( ParamBuffer* _param_buffer )
{
	ID3D11Buffer* buffer = m_shader_param.get();

	Device::updateResourceData<ParamBuffer>( buffer, *_param_buffer );

	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 2, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 2, 1, &buffer );

	return true;
}

void DMClipMapShader::updateMaterials( std::vector<Material>& materials )
{
	m_materials.updateData( &materials[0], sizeof( Material ) * materials.size() );

	m_materials.setToSlot( 3, SRVType::ps );
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

bool DMClipMapShader::innerInitialize()
{
	createPhase( 0, 0 );

	return true;
}