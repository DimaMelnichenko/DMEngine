#include "DMTexture.h"
#include <D3DX11tex.h>

namespace GS
{

DMTexture::DMTexture( uint32_t id, const std::string& name, ScratchImage&& image ) :
	DMResource( id, name ),
	m_image( std::move(image) )
{
}

DMTexture::DMTexture( DMTexture&& obj ) : DMResource( obj.id(), obj.name() ), m_srv( nullptr )
{
	std::swap( this->m_srv, obj.m_srv );
}

DMTexture::~DMTexture()
{

}

ID3D11ShaderResourceView* DMTexture::srv()
{
	return m_srv.get();
}

bool DMTexture::createSRV()
{
	ID3D11ShaderResourceView* srv = nullptr;
	HRESULT hr = CreateShaderResourceView( DMD3D::instance().GetDevice(),
										   m_image.GetImages(), m_image.GetImageCount(),
										   m_image.GetMetadata(), &srv );
	if( FAILED( hr ) )
	{
		return false;
	}

	m_srv = make_com_ptr<ID3D11ShaderResourceView>( srv );

	return true;
}

}
