#include "DMGrassShader.h"


DMGrassShader::DMGrassShader()
{

}

DMGrassShader::~DMGrassShader()
{
	
}

void DMGrassShader::StrimOutputDeclaration( D3D11_SO_DECLARATION_ENTRY* entry )
{
	entry->Stream = 0;
	entry->SemanticName = "POSITION";
	entry->SemanticIndex = 0;
	entry->StartComponent = 0;
	entry->ComponentCount = 3;
	entry->OutputSlot = 0;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMGrassShader::initLayouts()
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

	if( !m_param_buffer )
		DMD3D::instance().createShaderConstantBuffer( sizeof( GrassParam ), m_param_buffer );

	return vertex_layout;
}

bool DMGrassShader::prepare()
{
	if( phase() == 0 )
		DMD3D::instance().GetDeviceContext()->IASetInputLayout( nullptr );

	return true;
}

void DMGrassShader::prepareRender()
{
	return;
}

void DMGrassShader::Update( float )
{

}

void DMGrassShader::setTextureDistribution( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->GSSetShaderResources( 0, 1, &texture );
}

void DMGrassShader::setTextureHeight( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->GSSetShaderResources( 1, 1, &texture );
}

void DMGrassShader::setTextureColor( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &texture );
}

void DMGrassShader::setTextureNoise( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->GSSetShaderResources( 3, 1, &texture );
}

void DMGrassShader::setTerrainColor( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->GSSetShaderResources( 2, 1, &texture );
}

void DMGrassShader::setMapSize( float map_size )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( m_param_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	GrassParam* dataPtr = (GrassParam*)mappedResource.pData;

	// Copy the matrices into the constant buffer.
	dataPtr->cb_map_size = map_size;

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( m_param_buffer.get(), 0 );
	

	ID3D11Buffer* buffer = m_param_buffer.get();
	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 2, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->GSSetConstantBuffers( 2, 1, &buffer );
}