#include "DMD3D.h"
#include "..\..\..\Utils\utilites.h"


DMD3D::DMD3D(  )
{
	m_swapChain = 0;
	m_device = 0;
	m_deviceContext = 0;
	m_renderTargetView = 0;
	m_depthStencilBuffer = 0;
	m_depthStencilState = 0;
	m_depthStencilView = 0;
	m_rasterState = 0;
	m_rasterStateNoCulling = 0;

	m_depthDisabledStencilState = 0;

	m_alphaEnableBlendingState = 0;
	m_alphaDisableBlendingState = 0;
	m_blendStateTransparency = 0;
}

DMD3D::~DMD3D()
{
	Shutdown();
}

bool DMD3D::Initialize( int screenWidth, int screenHeight, bool vsync, HWND hwnd, bool fullscreen, float screenDepth, float screenNear )
{
	HRESULT result;
	IDXGIFactory* factory;
	IDXGIAdapter* adapter;
	IDXGIOutput* adapterOutput;
	unsigned int numModes, i, numerator, denominator, stringLength;
	numModes = i = numerator = denominator = stringLength = 0;
	DXGI_MODE_DESC* displayModeList;
	DXGI_ADAPTER_DESC adapterDesc;
	int error;
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	D3D_FEATURE_LEVEL featureLevel;
	ID3D11Texture2D* backBufferPtr;
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	D3D11_RASTERIZER_DESC rasterDesc;	
	D3D11_DEPTH_STENCIL_DESC depthDisabledStencilDesc;
	D3D11_BLEND_DESC blendStateDescription;

	// Store the vsync setting.
	m_vsync_enabled = vsync;

	// Create a DirectX graphics interface factory.
	result = CreateDXGIFactory( __uuidof( IDXGIFactory ), (void**)&factory );
	if( FAILED( result ) )
	{
		return false;
	}

	// Use the factory to create an adapter for the primary graphics interface (video card).
	result = factory->EnumAdapters( 0, &adapter );
	if( FAILED( result ) )
	{
		return false;
	}

	// Enumerate the primary adapter output (monitor).
	result = adapter->EnumOutputs( 0, &adapterOutput );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	result = adapterOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	// Create a list to hold all the possible display modes for this monitor/video card combination.
	displayModeList = new DXGI_MODE_DESC[numModes];
	if( !displayModeList )
	{
		return false;
	}

	// Now fill the display mode list structures.
	result = adapterOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, displayModeList );
	if( FAILED( result ) )
	{
		return false;
	}

	// Now go through all the display modes and find the one that matches the screen width and height.
	// When a match is found store the numerator and denominator of the refresh rate for that monitor.
	for( i = 0; i < numModes; i++ )
	{
		if( displayModeList[i].Width == (unsigned int)screenWidth )
		{
			if( displayModeList[i].Height == (unsigned int)screenHeight )
			{
				numerator = displayModeList[i].RefreshRate.Numerator;
				denominator = displayModeList[i].RefreshRate.Denominator;
			}
		}
	}

	// Get the adapter (video card) description.
	result = adapter->GetDesc( &adapterDesc );
	if( FAILED( result ) )
	{
		return false;
	}

	// Store the dedicated video card memory in megabytes.
	m_videoCardMemory = (int)( adapterDesc.DedicatedVideoMemory / 1024 / 1024 );

	// Convert the name of the video card to a character array and store it.
	error = wcstombs_s( &stringLength, m_videoCardDescription, 128, adapterDesc.Description, 128 );
	if( error != 0 )
	{
		return false;
	}

	// Release the display mode list.
	delete[] displayModeList;
	displayModeList = 0;

	// Release the adapter output.
	adapterOutput->Release( );
	adapterOutput = 0;

	// Release the adapter.
	adapter->Release( );
	adapter = 0;

	// Release the factory.
	factory->Release( );
	factory = 0;

	// Initialize the swap chain description.
	ZeroMemory( &swapChainDesc, sizeof( swapChainDesc ) );

	// Set to a single back buffer.
	swapChainDesc.BufferCount = 1;

	// Set the width and height of the back buffer.
	swapChainDesc.BufferDesc.Width = screenWidth;
	swapChainDesc.BufferDesc.Height = screenHeight;

	// Set regular 32-bit surface for the back buffer.
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// Set the refresh rate of the back buffer.
	if( m_vsync_enabled )
	{
		swapChainDesc.BufferDesc.RefreshRate.Numerator = numerator;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = denominator;
	}
	else
	{
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	}

	// Set the usage of the back buffer.
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	// Set the handle for the window to render to.
	swapChainDesc.OutputWindow = hwnd;

	// Turn multisampling off.
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	// Set to full screen or windowed mode.
	if( fullscreen )
	{
		swapChainDesc.Windowed = false;
	}
	else
	{
		swapChainDesc.Windowed = true;
	}

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	// Discard the back buffer contents after presenting.
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// Don't set the advanced flags.
	swapChainDesc.Flags = 0;

	// Set the feature level to DirectX 11.
	featureLevel = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	result = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, &featureLevel, 1,
											D3D11_SDK_VERSION, &swapChainDesc, &m_swapChain, &m_device, NULL, &m_deviceContext );
	if( FAILED( result ) )
	{
		featureLevel = D3D_FEATURE_LEVEL_10_1;
		result = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, &featureLevel, 1,
												D3D11_SDK_VERSION, &swapChainDesc, &m_swapChain, &m_device, NULL, &m_deviceContext );
		if( FAILED( result ) )
			return false;
	}

	m_interfaces.push_front( m_swapChain );
	m_interfaces.push_front( m_device );
	m_interfaces.push_front( m_deviceContext );

	D3D_FEATURE_LEVEL lvl = m_device->GetFeatureLevel();

	// Get the pointer to the back buffer.
	result = m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&backBufferPtr );
	if( FAILED( result ) )
	{
		return false;
	}

	// Create the render target view with the back buffer pointer.
	result = m_device->CreateRenderTargetView( backBufferPtr, NULL, &m_renderTargetView );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_renderTargetView  );

	// Release pointer to the back buffer as we no longer need it.
	backBufferPtr->Release( );
	backBufferPtr = 0;

	// Initialize the description of the depth buffer.
	ZeroMemory( &depthBufferDesc, sizeof( depthBufferDesc ) );

	// Set up the description of the depth buffer.
	depthBufferDesc.Width = screenWidth;
	depthBufferDesc.Height = screenHeight;
	depthBufferDesc.MipLevels = 1;
	depthBufferDesc.ArraySize = 1;
	depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.SampleDesc.Count = 1;
	depthBufferDesc.SampleDesc.Quality = 0;
	depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthBufferDesc.CPUAccessFlags = 0;
	depthBufferDesc.MiscFlags = 0;

	// Create the texture for the depth buffer using the filled out description.
	result = m_device->CreateTexture2D( &depthBufferDesc, NULL, &m_depthStencilBuffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_depthStencilBuffer );

	// Initialize the description of the stencil state.
	ZeroMemory( &depthStencilDesc, sizeof( depthStencilDesc ) );

	// Set up the description of the stencil state.
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

	depthStencilDesc.StencilEnable = true;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing.
	depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing.
	depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Create the depth stencil state.
	result = m_device->CreateDepthStencilState( &depthStencilDesc, &m_depthStencilState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_depthStencilState );

	// Set the depth stencil state.
	m_deviceContext->OMSetDepthStencilState( m_depthStencilState, 1 );
	//m_deviceContext->OMSetDepthStencilState( nullptr, 0 );


	// Initailze the depth stencil view.
	ZeroMemory( &depthStencilViewDesc, sizeof( depthStencilViewDesc ) );

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	result = m_device->CreateDepthStencilView( m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_depthStencilView );

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_deviceContext->OMSetRenderTargets( 1, &m_renderTargetView, m_depthStencilView );


	// Setup the raster description which will determine how and what polygons will be drawn.
	rasterDesc.AntialiasedLineEnable = false;
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = 0;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = true;
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.FrontCounterClockwise = false;
	rasterDesc.MultisampleEnable = true;
	rasterDesc.ScissorEnable = false;
	rasterDesc.SlopeScaledDepthBias = 0.0f;


	// Create the rasterizer state from the description we just filled out.
	result = m_device->CreateRasterizerState( &rasterDesc, &m_rasterState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_rasterState );

	// Now set the rasterizer state.
	m_deviceContext->RSSetState( m_rasterState );


	// create back face culling state
	rasterDesc.CullMode = D3D11_CULL_FRONT;
	// Create the rasterizer state from the description we just filled out.
	result = m_device->CreateRasterizerState( &rasterDesc, &m_raster_state_front_culling );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_raster_state_front_culling );

	// create shadow render state
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = 0;
	rasterDesc.SlopeScaledDepthBias = 5.5f;
	// Create the rasterizer state from the description we just filled out.
	result = m_device->CreateRasterizerState( &rasterDesc, &m_raster_state_shadow );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_raster_state_shadow );


	// Setup a raster description which turns off back face culling.
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthBias = 0;
	rasterDesc.SlopeScaledDepthBias = 0.0f;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = TRUE;
	rasterDesc.ScissorEnable = FALSE;
	rasterDesc.MultisampleEnable = TRUE;
	rasterDesc.AntialiasedLineEnable = FALSE;

	// Create the no culling rasterizer state.
	result = m_device->CreateRasterizerState( &rasterDesc, &m_rasterStateNoCulling );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_rasterStateNoCulling );

	// Setup a raster description which turns off back face culling.
	rasterDesc.AntialiasedLineEnable = false;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.DepthBias = 0;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = true;
	rasterDesc.FillMode = D3D11_FILL_WIREFRAME;
	rasterDesc.FrontCounterClockwise = false;
	rasterDesc.MultisampleEnable = false;
	rasterDesc.ScissorEnable = false;
	rasterDesc.SlopeScaledDepthBias = 0.0f;

	// Create the no culling rasterizer state.
	result = m_device->CreateRasterizerState( &rasterDesc, &m_rasterStateWireframe );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_rasterStateWireframe );

	// Setup the viewport for rendering.
	m_viewport.Width = (float)screenWidth;
	m_viewport.Height = (float)screenHeight;
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;

	// Create the viewport.
	m_deviceContext->RSSetViewports( 1, &m_viewport );

	// Clear the second depth stencil state before setting the parameters.
	ZeroMemory( &depthDisabledStencilDesc, sizeof( depthDisabledStencilDesc ) );

	// Now create a second depth stencil state which turns off the Z buffer for 2D rendering.  The only difference is 
	// that DepthEnable is set to false, all other parameters are the same as the other depth stencil state.
	depthDisabledStencilDesc.DepthEnable = false;
	depthDisabledStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthDisabledStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
	depthDisabledStencilDesc.StencilEnable = true;
	depthDisabledStencilDesc.StencilReadMask = 0xFF;
	depthDisabledStencilDesc.StencilWriteMask = 0xFF;
	depthDisabledStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthDisabledStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	depthDisabledStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthDisabledStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthDisabledStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Create the state using the device.
	result = m_device->CreateDepthStencilState( &depthDisabledStencilDesc, &m_depthDisabledStencilState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_depthDisabledStencilState );

	// Clear the blend state description.
	ZeroMemory( &blendStateDescription, sizeof( D3D11_BLEND_DESC ) );

	// Create an alpha enabled blend state description.
	//blendStateDescription.AlphaToCoverageEnable = true;	
	blendStateDescription.RenderTarget[0].BlendEnable = TRUE;
	blendStateDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;	
	blendStateDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].RenderTargetWriteMask = 0X0f;
	

	// Create the blend state using the description.
	result = m_device->CreateBlendState( &blendStateDescription, &m_alphaEnableBlendingState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_alphaEnableBlendingState );

	// Modify the description to create an alpha disabled blend state description.
	blendStateDescription.AlphaToCoverageEnable = false;
	blendStateDescription.RenderTarget[0].BlendEnable = FALSE;

	// Create the blend state using the description.
	result = m_device->CreateBlendState( &blendStateDescription, &m_alphaDisableBlendingState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_alphaDisableBlendingState );

	// Create a secondary alpha blend state description.
	blendStateDescription.AlphaToCoverageEnable = true;
	blendStateDescription.RenderTarget[0].BlendEnable = TRUE;
	blendStateDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].RenderTargetWriteMask = 0x0f;

	// Create the blend state using the description.
	result = m_device->CreateBlendState( &blendStateDescription, &m_blendStateTransparency );
	if( FAILED( result ) )
	{
		return false;
	}

	m_interfaces.push_front( m_blendStateTransparency );

	return true;
}

void DMD3D::Shutdown( )
{
	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.

	if( m_swapChain )
	{
		m_swapChain->SetFullscreenState( false, NULL );		
	}

	for( IUnknown* object : m_interfaces )
	{
		object->Release();
	}

	return;
}

void DMD3D::TurnOnAlphaBlending( )
{
	float blendFactor[4];


	// Setup the blend factor.
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	// Turn on the alpha blending.
	m_deviceContext->OMSetBlendState( m_alphaEnableBlendingState, blendFactor, 0xffffffff );

	return;
}

void DMD3D::TurnOffAlphaBlending( )
{
	float blendFactor[4];


	// Setup the blend factor.
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	// Turn off the alpha blending.
	m_deviceContext->OMSetBlendState( m_alphaDisableBlendingState, blendFactor, 0xffffffff );

	return;
}

void DMD3D::BeginScene( float red, float green, float blue, float alpha )
{
	float color[4];


	// Setup the color to clear the buffer to.
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;

	// Clear the back buffer.
	m_deviceContext->ClearRenderTargetView( m_renderTargetView, color );

	// Clear the depth buffer.
	m_deviceContext->ClearDepthStencilView( m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0 );

	return;
}

void DMD3D::EndScene( )
{
	// Present the back buffer to the screen since rendering is complete.
	if( m_vsync_enabled )
	{
		// Lock to screen refresh rate.
		m_swapChain->Present( 1, 0 );
	}
	else
	{
		// Present as fast as possible.
		m_swapChain->Present( 0, 0 );
	}


	// clear all slots and resources
	
	static ID3D11Buffer** buffers = new ID3D11Buffer*[10];
	static ID3D11ShaderResourceView** views = new ID3D11ShaderResourceView*[50];
	static UINT* strides = new UINT[10];
	static UINT* offsets = new UINT[10];

	static bool first = true;
	
	if( first )
	{
		for( size_t i = 0; i < 10; i++ )
		{
			buffers[i] = nullptr;
			strides[i] = 0;
			offsets[i] = 0;
		}

		for( size_t i = 0; i < 50; i++ )
		{
			views[i] = nullptr;
		}

		first = false;
	}
	
	//m_deviceContext->IASetVertexBuffers( 0, 10, buffers, strides, offsets );
	m_deviceContext->VSSetShaderResources( 0, 50, views );
	m_deviceContext->GSSetShaderResources( 0, 50, views );
	m_deviceContext->PSSetShaderResources( 0, 50, views );
	
	return;
}

ID3D11Device* DMD3D::GetDevice( )
{
	return m_device;
}

ID3D11DeviceContext* DMD3D::GetDeviceContext( )
{
	return m_deviceContext;
}

void DMD3D::GetVideoCardInfo( char* cardName, int& memory )
{
	strcpy_s( cardName, 128, m_videoCardDescription );
	memory = m_videoCardMemory;
	return;
}

void DMD3D::TurnZBufferOn( )
{
	m_deviceContext->OMSetDepthStencilState( m_depthStencilState, 1 );
	return;
}

void DMD3D::TurnZBufferOff( )
{
	m_deviceContext->OMSetDepthStencilState( m_depthDisabledStencilState, 1 );
	return;
}

ID3D11DepthStencilView* DMD3D::GetDepthStencilView( )
{
	return m_depthStencilView;
}

void DMD3D::SetBackBufferRenderTarget( )
{
	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_deviceContext->OMSetRenderTargets( 1, &m_renderTargetView, m_depthStencilView );

	return;
}

void DMD3D::ResetViewport( )
{
	// Set the viewport.
	m_deviceContext->RSSetViewports( 1, &m_viewport );

	return;
}

void DMD3D::TurnDefaultRS( )
{
	// Set the culling rasterizer state.
	m_deviceContext->RSSetState( m_rasterState );

	return;
}

void DMD3D::TurnShadowRS()
{
	// Set the culling rasterizer state.
	m_deviceContext->RSSetState( m_raster_state_shadow );

	return;
}

void DMD3D::TurnBackFacesRS()
{
	// Set the culling rasterizer state.
	m_deviceContext->RSSetState( m_raster_state_front_culling );

	return;
}

void DMD3D::TurnCullingNoneRS( )
{
	// Set the no back face culling rasterizer state.
	m_deviceContext->RSSetState( m_rasterStateNoCulling );

	return;
}

void DMD3D::TurnOnWireframe()
{
	m_deviceContext->RSSetState( m_rasterStateWireframe );
}

void DMD3D::TurnOffWireframe()
{
	m_deviceContext->RSSetState( m_rasterState );
}

void DMD3D::TurnOnTransparancy( )
{
	float blendFactor[4];


	// Setup the blend factor.
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	// Turn on the alpha blending.
	m_deviceContext->OMSetBlendState( m_blendStateTransparency, blendFactor, 0xffffffff );

	return;
}

void DMD3D::TurnOffTransparancy()
{
	TurnOffAlphaBlending();
}

bool DMD3D::createShaderConstantBuffer( size_t byte_size, com_unique_ptr<ID3D11Buffer>& shared_buffer,
										const D3D11_SUBRESOURCE_DATA * srData )
{
	D3D11_BUFFER_DESC param_buffer_desc;
	param_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	param_buffer_desc.ByteWidth = byte_size;
	param_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	param_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	param_buffer_desc.MiscFlags = 0;
	param_buffer_desc.StructureByteStride = 0;
	
	ID3D11Buffer* buffer;
	HRESULT result = GetDevice()->CreateBuffer( &param_buffer_desc, srData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	shared_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	return true;
}


bool DMD3D::CreateBuffer( const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData,  com_unique_ptr<ID3D11Buffer>& created_buffer )
{
	ID3D11Buffer* buffer;
	HRESULT result = m_device->CreateBuffer( pDesc, pInitialData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	created_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	return true;
}