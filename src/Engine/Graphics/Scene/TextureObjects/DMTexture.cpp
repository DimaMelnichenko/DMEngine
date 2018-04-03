#include "DMTexture.h"
#include <D3DX11tex.h>

namespace GS
{

DMTexture::DMTexture( uint32_t id, ID3D11ShaderResourceView* texture ) :
	DMResource( id ),
	m_texture( make_com_ptr<ID3D11ShaderResourceView>( texture ) )
{
}

DMTexture::DMTexture( DMTexture&& obj ) : DMResource( obj.id() ), m_texture( nullptr )
{
	std::swap( this->m_texture, obj.m_texture );
}

DMTexture::~DMTexture()
{

}

ID3D11ShaderResourceView* DMTexture::GetTexture()
{
	return m_texture.get();
}

}
