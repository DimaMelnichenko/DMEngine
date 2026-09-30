#include "RenderTarget.h"
#include "DMD3D.h"

bool RenderTarget::create( uint32_t width, uint32_t height, DXGI_FORMAT format )
{
	TextureDesc desc;
	desc.width = width;
	desc.height = height;
	desc.format = format;
	desc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource;

	DMD3D& d3d = DMD3D::instance();
	return d3d.createTexture( desc, nullptr, m_texture ) &&
		   d3d.createTargetView( m_texture, {}, m_target ) &&
		   d3d.createShaderView( m_texture, {}, m_srv );
}
