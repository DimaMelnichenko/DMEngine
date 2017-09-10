#include "DMRenderCubeTexture.h"



DMRenderCubeTexture::DMRenderCubeTexture( DMD3D* dmd3d ) : DMRenderTexture( dmd3d )
{
}


DMRenderCubeTexture::~DMRenderCubeTexture()
{
}

bool DMRenderCubeTexture::Initialize( DXGI_FORMAT format, unsigned int textureWidth, unsigned int textureHeight )
{
	m_width = textureWidth;
	m_height = textureHeight;

	createRenderTarget( format );

	inintDepthStencilBuffer();

	inintDSV( m_depthStencilBuffer.get() );

	setupViewport();

	initCameras();

	return true;
}

bool DMRenderCubeTexture::createRenderTarget( DXGI_FORMAT format )
{
	HRESULT result;
	D3D11_TEXTURE2D_DESC textureDesc;	
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;

	// Initialize the render target texture description.
	ZeroMemory( &textureDesc, sizeof( D3D11_TEXTURE2D_DESC ) );

	// Setup the render target texture description.
	textureDesc.Width = m_width;
	textureDesc.Height = m_height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 6;
	textureDesc.Format = format;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	ID3D11Texture2D* texture;
	// Create the render target texture.
	result = m_dmd3d->GetDevice()->CreateTexture2D( &textureDesc, NULL, &texture );
	if( FAILED( result ) )
	{
		return false;
	}

	m_renderTargetTexture = make_com_ptr<ID3D11Texture2D>( texture );

	// Setup the description of the render target views.
	
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;

	for( size_t i = 0; i < 6; i++ )
	{
		//renderTargetViewDesc.Texture2D.MipSlice = D3D11CalcSubresource( 0, i, 1 );
		renderTargetViewDesc.Texture2DArray.ArraySize = 1;
		renderTargetViewDesc.Texture2DArray.FirstArraySlice = i;
		renderTargetViewDesc.Texture2DArray.MipSlice = 0;

		ID3D11RenderTargetView* target_view;
		// Create the render target view.
		result = m_dmd3d->GetDevice()->CreateRenderTargetView( texture, &renderTargetViewDesc, &target_view );
		if( FAILED( result ) )
		{
			return false;
		}

		m_renderTargetViews.push_back( make_com_ptr<ID3D11RenderTargetView>( target_view ) );
	}

	// Setup the description of the shader resource view.
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	shaderResourceViewDesc.TextureCube.MostDetailedMip = 0;
	shaderResourceViewDesc.TextureCube.MipLevels = 1;

	ID3D11ShaderResourceView* resource_view;
	// Create the shader resource view.
	result = m_dmd3d->GetDevice()->CreateShaderResourceView( texture, &shaderResourceViewDesc, &resource_view );
	if( FAILED( result ) )
	{
		return false;
	}

	m_shaderResourceView = make_com_ptr<ID3D11ShaderResourceView>( resource_view );

	return true;
}

bool DMRenderCubeTexture::initLoop( const D3DXVECTOR3& pos )
{
	return initLoop( pos.x, pos.y, pos.z );
}

bool DMRenderCubeTexture::initLoop( float x, float y, float z )
{
	m_loop_index = -1;

	for( auto& camera : m_cameras )
	{
		camera->SetPosition( x, y, z );
		camera->Render();
	}

	return true;
}

DMCamera* DMRenderCubeTexture::next()
{
	if( ++m_loop_index < 6 )
	{
		SetRenderTarget();

		ClearRenderTarget( 0.0, 0.0, 0.0, 1.0f );

		return m_cameras[m_loop_index].get();
	}

	return nullptr;
}

void DMRenderCubeTexture::SetRenderTarget( bool only_depth )
{
	ID3D11RenderTargetView* view = m_renderTargetViews[m_loop_index].get();

	// Bind the render target view and depth stencil buffer to the output render pipeline.

	if( only_depth )
	{
		ID3D11RenderTargetView* pnullView = NULL;
		m_dmd3d->GetDeviceContext()->OMSetRenderTargets( 1, &pnullView, m_depthStencilView.get() );
	}
	else
		m_dmd3d->GetDeviceContext()->OMSetRenderTargets( 1, &view, m_depthStencilView.get() );


	// Set the viewport.
	m_dmd3d->GetDeviceContext()->RSSetViewports( 1, &m_viewport );

	return;
}

void DMRenderCubeTexture::initCameras()
{
	std::unique_ptr<DMCamera> camera( new DMCamera() );

	// right
	camera->Initialize( DMCamera::CT_PERSPECTIVE, m_width, m_height, 1.0, 500.0, D3DX_PI/2.0 );
	camera->SetRotation( 0.0, 90.0, 0.0 ); 

	m_cameras.push_back( std::move( camera ) );

	// left
	camera = std::unique_ptr<DMCamera>( new DMCamera() );
	camera->Initialize( DMCamera::CT_PERSPECTIVE, m_width, m_height, 1.0, 500.0, D3DX_PI / 2.0 );
	camera->SetRotation( 0.0, -90.0, 0.0 );

	m_cameras.push_back( std::move( camera ) );

	// up
	camera = std::unique_ptr<DMCamera>( new DMCamera() );
	camera->Initialize( DMCamera::CT_PERSPECTIVE, m_width, m_height, 1.0, 500.0, D3DX_PI / 2.0 );
	camera->SetRotation( -89.98f, 0.0, 0.0 );

	m_cameras.push_back( std::move( camera ) );

	// down
	camera = std::unique_ptr<DMCamera>( new DMCamera() );
	camera->Initialize( DMCamera::CT_PERSPECTIVE, m_width, m_height, 1.0, 500.0, D3DX_PI / 2.0 );
	camera->SetRotation( 89.98f, 0.0, 0.0 );

	m_cameras.push_back( std::move( camera ) );

	// front
	camera = std::unique_ptr<DMCamera>( new DMCamera() );
	camera->Initialize( DMCamera::CT_PERSPECTIVE, m_width, m_height, 1.0, 500.0, D3DX_PI / 2.0 );
	camera->SetRotation( 0.0, 0.0, 0.0 );

	m_cameras.push_back( std::move( camera ) );

	// back
	camera = std::unique_ptr<DMCamera>( new DMCamera() );
	camera->Initialize( DMCamera::CT_PERSPECTIVE, m_width, m_height, 1.0, 500.0, D3DX_PI / 2.0 );
	camera->SetRotation( 0.0, 180.0, 0.0 );

	m_cameras.push_back( std::move( camera ) );
}

void DMRenderCubeTexture::ClearRenderTarget( float red, float green, float blue, float alpha )
{
	float color[4];

	// Setup the color to clear the buffer to.
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;


	m_dmd3d->GetDeviceContext()->ClearRenderTargetView( m_renderTargetViews[m_loop_index].get(), color );

	// Clear the depth buffer.
	m_dmd3d->GetDeviceContext()->ClearDepthStencilView( m_depthStencilView.get(), D3D11_CLEAR_DEPTH, 1.0f, 0 );

	return;


}

ID3D11ShaderResourceView* DMRenderCubeTexture::GetShaderResourceView()
{
	return m_shaderResourceView.get();
}