#include "DMTexture2D.h"


DMTexture2D::DMTexture2D(  )
{
	m_texture = 0;
}


DMTexture2D::~DMTexture2D()
{
	if( m_texture )
	{
		m_texture->Release();
		m_texture = 0;
	}
}

bool DMTexture2D::Initialize( ID3D11Device* device, ID3D11DeviceContext* _device_context, const WCHAR* filename )
{

	return true;
	/*
	HRESULT result;

	ID3D11ShaderResourceView* resource_view;

	// Load the texture in.
	result = D3DX11CreateShaderResourceViewFromFile( device, filename, NULL, NULL, &resource_view, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	
	resource_view->GetResource( (ID3D11Resource**)( &m_texture ) );
	
	D3D11_TEXTURE2D_DESC desc;

	m_texture->GetDesc( &desc );


	_device_context->CopySubresourceRegion(  )


	D3D11_MAPPED_SUBRESOURCE texmap;
	_device_context->Map( m_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &texmap );

	UINT32* data = ( UINT32* )texmap.pData;
	UINT32 color = data[0];
	
	for( UINT i = 0; i < desc.Width; i++ )
	{
		for( UINT j = 0; j < desc.Height; j++ )
		{			
			data[i][j]
		}
	}
	
	_device_context->Unmap( m_texture, 0 );

	resource_view->Release();

	return true;
	*/
}