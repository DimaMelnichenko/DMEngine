#include "DDSTexture.h"

namespace GS
{

DDSTexture::DDSTexture( uint32_t id, const std::string& name, ScratchImage&& image ) 
	: DMTexture( id, name ),
	m_image( std::move( image ) )
{
}

DDSTexture::DDSTexture( DDSTexture&& other ) : DMTexture( std::move( other ) )
{
	std::swap( m_image, other.m_image );
	std::swap( other.m_srv, m_srv );
}

DDSTexture::~DDSTexture()
{
}

const ScratchImage& DDSTexture::image() const
{
	return m_image;
}

bool DDSTexture::createSRV()
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

uint32_t DDSTexture::height() const
{
	return m_image.GetMetadata().height;
}

uint32_t DDSTexture::width() const
{
	return m_image.GetMetadata().height;
}

const com_unique_ptr<ID3D11ShaderResourceView>& DDSTexture::srv() const
{
	return m_srv;
}

}