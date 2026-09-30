#include "CustomTexture.h"
#include "Logger\Logger.h"
#include <random>

namespace GS
{

CustomTexture::CustomTexture( uint32_t id, const std::string& name ) : DMTexture( id, name )
{

}

CustomTexture::CustomTexture( CustomTexture&& other ) : DMTexture( std::move(other) )
{
	std::swap( other.m_texture, m_texture );
	std::swap( other.m_srv, m_srv );
}

CustomTexture::~CustomTexture()
{
}

const ShaderView& CustomTexture::srv() const
{
	return m_srv;
}

const Texture& CustomTexture::texture() const
{
	return m_texture;
}

bool CustomTexture::generateTexture()
{
	// Монохромный шум R8_SNORM — один и тот же при каждом запуске
	std::vector<uint8_t> buf( m_width * m_hight, 0 );
	std::mt19937 generator( 1 );
	std::uniform_int_distribution<int> dist( 0, 255 );
	for( uint32_t i = 0; i < ( m_hight * m_width ); i++ )
		buf[i] = dist( generator );

	TextureDesc desc;
	desc.width = m_width;
	desc.height = m_hight;
	desc.format = DXGI_FORMAT_R8_SNORM;
	desc.usage = TextureUsage::shaderResource;

	TextureData data;
	data.data = buf.data();
	data.rowPitch = m_width;

	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createTexture( desc, &data, m_texture ) )
	{
		LOG( "Failed create CreateTexture2D" );
		return false;
	}
	if( !d3d.createShaderView( m_texture, {}, m_srv ) )
	{
		LOG( "Can't create Shader Resource View" );
		return false;
	}

	return true;
}

}
