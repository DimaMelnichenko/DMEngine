#include "DMRenderTextureMRT.h"



DMRenderTextureMRT::DMRenderTextureMRT( DMD3D* parent ) : DMRenderTexture( parent )
{
}


DMRenderTextureMRT::~DMRenderTextureMRT()
{
	
}

bool DMRenderTextureMRT::Initialize( DXGI_FORMAT _format, unsigned int textureWidth, unsigned int textureHeight )
{
	m_width = textureWidth;
	m_height = textureHeight;

	addRenderTarget( _format );

	inintDepthStencilBuffer();

	inintDSV( m_depthStencilBuffer.get() );

	setupViewport();

	return true;
}

ID3D11ShaderResourceView* DMRenderTextureMRT::GetShaderResourceView( int index )
{
	return m_shaderResourceViews[index].get();
}

void DMRenderTextureMRT::SetRenderTarget( bool only_depth )
{
	static ID3D11RenderTargetView* views[10];

	for( size_t i = 0; i < m_renderTargetViews.size(); i++ )
	{
		views[i] = m_renderTargetViews[i].get();
	}

	// Bind the render target view and depth stencil buffer to the output render pipeline.

	if( only_depth )
	{
		ID3D11RenderTargetView* pnullView = NULL;
		m_dmd3d->GetDeviceContext()->OMSetRenderTargets( 1, &pnullView, m_depthStencilView.get() );
	}
	else
		m_dmd3d->GetDeviceContext()->OMSetRenderTargets( m_renderTargetViews.size(), views, m_depthStencilView.get() );


	// Set the viewport.
	m_dmd3d->GetDeviceContext()->RSSetViewports( 1, &m_viewport );

	return;
}

bool DMRenderTextureMRT::addRenderTarget( DXGI_FORMAT format )
{
	com_unique_ptr<ID3D11Texture2D> texture;
	com_unique_ptr<ID3D11RenderTargetView> rtv;
	com_unique_ptr<ID3D11ShaderResourceView> srv;

	if( !createRenderTarget( format, texture, rtv, srv ) )
	{
		return false;
	}

	m_renderTargetTextures.push_back( std::move( texture ) );
	m_renderTargetViews.push_back( std::move( rtv ) );
	m_shaderResourceViews.push_back( std::move( srv ) );
	
		
	return true;
}