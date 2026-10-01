#include "DMTexture.h"
#include "D3D\DMD3D.h"
#include "D3D\TextureImages.h"
#include "Logger\Logger.h"

namespace GS
{

DMTexture::DMTexture( uint32_t id, const std::string& name ) : DMResource( id, name )
{
}

bool DMTexture::create( const DirectX::ScratchImage& image )
{
	if( !GpuImages::createTexture( image, m_texture, m_srv ) )
		return false;
	DMD3D::instance().setName( m_texture, name() );
	return true;
}

bool DMTexture::create( const TextureDesc& desc, const TextureData& data )
{
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createTexture( desc, &data, m_texture ) )
	{
		LOG( "Can't create texture " + name() );
		return false;
	}
	d3d.setName( m_texture, name() );
	if( !d3d.createShaderView( m_texture, {}, m_srv ) )
	{
		LOG( "Can't create shader view of texture " + name() );
		return false;
	}
	return true;
}

}
