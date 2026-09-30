#include "RenderTarget.h"
#include "DMD3D.h"

bool RenderTarget::create( uint32_t width, uint32_t height, DXGI_FORMAT format, const char* name )
{
	TextureDesc desc;
	desc.width = width;
	desc.height = height;
	desc.format = format;
	desc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource;

	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createTexture( desc, nullptr, m_texture ) )
		return false;
	if( name )
		d3d.setName( m_texture, name );
	return d3d.createTargetView( m_texture, {}, m_target ) &&
		   d3d.createShaderView( m_texture, {}, m_srv );
}
