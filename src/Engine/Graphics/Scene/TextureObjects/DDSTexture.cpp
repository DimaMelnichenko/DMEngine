#include "DDSTexture.h"
#include "D3D\TextureImages.h"

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
	std::swap( other.m_texture, m_texture );
	std::swap( other.m_srv, m_srv );
}

DDSTexture::~DDSTexture()
{
}

bool DDSTexture::createSRV()
{
	return GpuImages::createTexture( m_image, m_texture, m_srv );
}

uint32_t DDSTexture::height() const
{
	return m_image.GetMetadata().height;
}

uint32_t DDSTexture::width() const
{
	return m_image.GetMetadata().width;
}

const ShaderView& DDSTexture::srv() const
{
	return m_srv;
}

const Texture& DDSTexture::texture() const
{
	return m_texture;
}

}
