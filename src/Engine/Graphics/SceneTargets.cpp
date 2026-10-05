#include "SceneTargets.h"
#include <cstring>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

namespace GS
{

TargetFormats SceneTargets::formats()
{
	return TargetFormats::colorTarget( colorFormat, depthFormat );
}

TargetFormats SceneTargets::depthOnlyFormats()
{
	return TargetFormats::depthTarget( depthFormat );
}

bool SceneTargets::create( uint32_t width, uint32_t height, const float clearColor[4] )
{
	DMD3D& d3d = DMD3D::instance();
	// Прежние цели отпускаются отложенно, когда GPU закончит кадр, в котором они ещё рисовались
	m_colorView.reset();
	m_depthView.reset();
	m_colorCopyView.reset();
	m_colorCopy.reset();
	m_colorTarget.reset();
	m_depthTarget.reset();
	m_depthReadTarget.reset();
	m_color.reset();
	m_depth.reset();
	m_width = width;
	m_height = height;
	memcpy( m_clearColor, clearColor, sizeof( m_clearColor ) );

	TextureDesc colorDesc;
	colorDesc.width = width;
	colorDesc.height = height;
	colorDesc.format = colorFormat;
	colorDesc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource;
	colorDesc.hasClearColor = true;
	memcpy( colorDesc.clearColor, clearColor, sizeof( colorDesc.clearColor ) );
	TextureDesc depthDesc;
	depthDesc.width = width;
	depthDesc.height = height;
	depthDesc.format = depthTextureFormat;
	depthDesc.usage = TextureUsage::depthStencil | TextureUsage::shaderResource;
	TextureViewDesc depthReadDesc;
	depthReadDesc.readOnlyDepth = true;
	TextureViewDesc depthViewDesc;
	depthViewDesc.format = depthViewFormat;
	if( !d3d.createTexture( colorDesc, nullptr, m_color ) || !d3d.createTargetView( m_color, {}, m_colorTarget ) ||
		!d3d.createShaderView( m_color, {}, m_colorView ) || !d3d.createTexture( depthDesc, nullptr, m_depth ) ||
		!d3d.createTargetView( m_depth, {}, m_depthTarget ) || !d3d.createTargetView( m_depth, depthReadDesc, m_depthReadTarget ) ||
		!d3d.createShaderView( m_depth, depthViewDesc, m_depthView ) )
	{
		LOG( "Failed to create the scene color and depth buffers " + std::to_string( width ) + "x" + std::to_string( height ) );
		return false;
	}
	d3d.setName( m_color, "Scene color" );
	d3d.setName( m_depth, "Scene depth" );
	return true;
}

void SceneTargets::clear() const
{
	DMD3D& d3d = DMD3D::instance();
	d3d.beginPass( PassDesc{ "Scene clear", { { &m_colorTarget, "scene color" } }, { &m_depthTarget, "scene depth" }, m_width, m_height } );
	d3d.clearTarget( m_colorTarget, m_clearColor );
	d3d.clearDepth( m_depthTarget, 0.0f );
}

void SceneTargets::copyColor()
{
	DMD3D& d3d = DMD3D::instance();
	if( !m_colorCopy.handle() )
	{
		TextureDesc desc;
		desc.width = m_width;
		desc.height = m_height;
		desc.format = colorFormat;
		if( !d3d.createTexture( desc, nullptr, m_colorCopy ) || !d3d.createShaderView( m_colorCopy, {}, m_colorCopyView ) )
		{
			LOG( "Failed to create the scene color copy " + std::to_string( m_width ) + "x" + std::to_string( m_height ) );
			m_colorCopy.reset();
			return;
		}
		d3d.setName( m_colorCopy, "Scene color copy" );
	}
	d3d.copyTexture( m_colorCopy, m_color, "Scene color copy" );
}

}
