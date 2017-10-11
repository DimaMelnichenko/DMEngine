#include "DMRenderTexture.h"



DMRenderTexture::DMRenderTexture()
{	

}

DMRenderTexture::~DMRenderTexture()
{

}

DMRenderTexture::DMRenderTexture( DMRenderTexture&& rtexture )
{
	std::swap( m_renderTargetTexture, rtexture.m_renderTargetTexture );
	std::swap( m_renderTargetView, rtexture.m_renderTargetView );
	std::swap( m_shaderResourceView, rtexture.m_shaderResourceView );
	std::swap( m_depthStencilBuffer, rtexture.m_depthStencilBuffer );
	std::swap( m_depthStencilView, rtexture.m_depthStencilView );
	std::swap( m_depthSRV, rtexture.m_depthSRV );
	std::swap( m_viewport, rtexture.m_viewport );
	std::swap( m_use_depth, rtexture.m_use_depth );
	std::swap( m_width, rtexture.m_width );
	std::swap( m_height, rtexture.m_height );
}

DMRenderTexture::DMRenderTexture( const DMRenderTexture& rtexture )
{
	m_renderTargetTexture = rtexture.m_renderTargetTexture;
	m_renderTargetView = rtexture.m_renderTargetView;
	m_shaderResourceView = rtexture.m_shaderResourceView;
	m_depthStencilBuffer = rtexture.m_depthStencilBuffer;
	m_depthStencilView = rtexture.m_depthStencilView;
	m_depthSRV = rtexture.m_depthSRV;
	m_viewport = rtexture.m_viewport;
	m_use_depth = rtexture.m_use_depth;
	m_width = rtexture.m_width;
	m_height = rtexture.m_height;
}

DMRenderTexture& DMRenderTexture::operator=( const DMRenderTexture& other )
{
	return *this;
}

bool DMRenderTexture::Initialize( DXGI_FORMAT _format, unsigned int textureWidth, unsigned int textureHeight, bool use_depth_buffer )
{
	m_use_depth = use_depth_buffer;
	m_width = textureWidth;
	m_height = textureHeight;

	createRenderTarget( _format );
	
	if( m_use_depth )
	{
		inintDepthStencilBuffer();
		inintDSV( m_depthStencilBuffer.get() );
	}

	setupViewport();

	return true;
}

void DMRenderTexture::setupViewport()
{
	// Setup the viewport for rendering.
	m_viewport.Width = (float)m_width;
	m_viewport.Height = (float)m_height;
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;
}

bool DMRenderTexture::inintDSV( ID3D11Texture2D* depthStencilBuffer )
{
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	// Initailze the depth stencil view description.
	ZeroMemory( &depthStencilViewDesc, sizeof( depthStencilViewDesc ) );

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

	ID3D11DepthStencilView* depth_stencil_view;
	// Create the depth stencil view.
	HRESULT result = DMD3D::instance().GetDevice()->CreateDepthStencilView( depthStencilBuffer, &depthStencilViewDesc, &depth_stencil_view );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthStencilView = make_com_ptr<ID3D11DepthStencilView>( depth_stencil_view );

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
	ZeroMemory( &SRVDesc, sizeof( D3D11_SHADER_RESOURCE_VIEW_DESC ) );
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MipLevels = 1;


	ID3D11ShaderResourceView* srv;
	result = DMD3D::instance().GetDevice()->CreateShaderResourceView( depthStencilBuffer, &SRVDesc, &srv );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthSRV = make_com_ptr<ID3D11ShaderResourceView>( srv );

	return true;
}

bool DMRenderTexture::inintDepthStencilBuffer( )
{
	D3D11_TEXTURE2D_DESC texture_depth_desc;

	// Initialize the description of the depth buffer.
	ZeroMemory( &texture_depth_desc, sizeof( texture_depth_desc ) );

	// Set up the description of the depth buffer.
	texture_depth_desc.Width = m_width;
	texture_depth_desc.Height = m_height;
	texture_depth_desc.MipLevels = 1;
	texture_depth_desc.ArraySize = 1;
	texture_depth_desc.Format = DXGI_FORMAT_R32_TYPELESS;
	texture_depth_desc.SampleDesc.Count = 1;
	texture_depth_desc.SampleDesc.Quality = 0;
	texture_depth_desc.Usage = D3D11_USAGE_DEFAULT;
	texture_depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	texture_depth_desc.CPUAccessFlags = 0;
	texture_depth_desc.MiscFlags = 0;

	ID3D11Texture2D* depth_stencil_buffer;
	// Create the texture for the depth buffer using the filled out description.
	HRESULT result = DMD3D::instance().GetDevice()->CreateTexture2D( &texture_depth_desc, NULL, &depth_stencil_buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthStencilBuffer = make_com_ptr<ID3D11Texture2D>( depth_stencil_buffer );

	return true;
}

bool DMRenderTexture::createRenderTarget( DXGI_FORMAT format )
{
	HRESULT result;
	D3D11_TEXTURE2D_DESC textureDesc;
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;

	// Initialize the render target texture description.
	ZeroMemory( &textureDesc, sizeof( D3D11_TEXTURE2D_DESC ) );

	// Setup the render target texture description.
	textureDesc.Width = m_width;
	textureDesc.Height = m_height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = format;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	ID3D11Texture2D* texture;
	// Create the render target texture.
	result = DMD3D::instance().GetDevice()->CreateTexture2D( &textureDesc, NULL, &texture );
	if( FAILED( result ) )
	{
		return false;
	}

	m_renderTargetTexture = make_com_sptr<ID3D11Texture2D>( texture );

	// Setup the description of the render target view.
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	ID3D11RenderTargetView* target_view;
	// Create the render target view.
	result = DMD3D::instance().GetDevice()->CreateRenderTargetView( texture, &renderTargetViewDesc, &target_view );
	if( FAILED( result ) )
	{
		return false;
	}

	m_renderTargetView =  make_com_sptr<ID3D11RenderTargetView>( target_view );

	// Setup the description of the shader resource view.
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* resource_view;
	// Create the shader resource view.
	result = DMD3D::instance().GetDevice()->CreateShaderResourceView( texture, &shaderResourceViewDesc, &resource_view );
	if( FAILED( result ) )
	{
		return false;
	}

	m_shaderResourceView = make_com_sptr<ID3D11ShaderResourceView>( resource_view );

	return true;
}

void DMRenderTexture::SetRenderTarget( bool only_depth ) const
{
	ID3D11RenderTargetView* view = m_renderTargetView.get();

	// Bind the render target view and depth stencil buffer to the output render pipeline.

	if( only_depth && m_use_depth )
	{
		ID3D11RenderTargetView* pnullView = NULL;
		DMD3D::instance().GetDeviceContext()->OMSetRenderTargets( 1, &pnullView, m_depthStencilView.get() );
	}
	else if( m_use_depth )
	{
		DMD3D::instance().GetDeviceContext()->OMSetRenderTargets( 1, &view, m_depthStencilView.get() );
	}
	else
	{
		DMD3D::instance().GetDeviceContext()->OMSetRenderTargets( 1, &view, nullptr );
	}
		

	// Set the viewport.
	DMD3D::instance().GetDeviceContext()->RSSetViewports( 1, &m_viewport );

	return;
}

void DMRenderTexture::ClearRenderTarget( float red, float green, float blue, float alpha ) const
{
	float color[4];

	// Setup the color to clear the buffer to.
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;
	
	
	DMD3D::instance().GetDeviceContext()->ClearRenderTargetView( m_renderTargetView.get(), color );

	// Clear the depth buffer.
	if( m_use_depth )
	{
		DMD3D::instance().GetDeviceContext()->ClearDepthStencilView( m_depthStencilView.get(), D3D11_CLEAR_DEPTH, 1.0f, 0 );
	}

	return;
}

ID3D11ShaderResourceView* DMRenderTexture::GetShaderResourceView() const
{
	return m_shaderResourceView.get();
}

ID3D11ShaderResourceView* DMRenderTexture::depthShaderResourceView() const
{
	return m_depthSRV.get();
}

unsigned int DMRenderTexture::width() const
{
	return m_width;
}
void DMRenderTexture::setWidth( float width )
{
	m_width = width;
}

void DMRenderTexture::setHeight( float height )
{
	m_height = height;
}

unsigned DMRenderTexture::height() const
{
	return m_height;
}

ID3D11Texture2D* DMRenderTexture::depthStencilBuffer()
{
	return m_depthStencilBuffer.get();
}

D3D11_VIEWPORT* DMRenderTexture::viewport()
{
	return &m_viewport;
}

ID3D11DepthStencilView* DMRenderTexture::depthStencilView()
{
	return m_depthStencilView.get();
}