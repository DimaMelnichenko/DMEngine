#include "DMTexture.h"
#include <D3DX11tex.h>



DMTexture::DMTexture( uint32_t id ) : DMResource(id)
{
}

DMTexture::DMTexture( DMTexture&& obj ) : DMResource( obj.id() ), m_texture(nullptr)
{
	std::swap( this->m_texture, obj.m_texture );
}

DMTexture::~DMTexture()
{

}

bool DMTexture::Initialize( const WCHAR* filename )
{
	HRESULT result;

	D3DX11_IMAGE_INFO imageInfo;
	// load image info
	D3DX11GetImageInfoFromFile( filename, nullptr, &imageInfo, nullptr );

	// Load the texture in.
	ID3D11ShaderResourceView* texture;
	result = D3DX11CreateShaderResourceViewFromFile( DMD3D::instance().GetDevice(), filename, NULL, NULL, &texture, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	m_texture = make_com_ptr<ID3D11ShaderResourceView>( texture );

	return true;
}

ID3D11ShaderResourceView* DMTexture::GetTexture()
{
	return m_texture.get();
}

