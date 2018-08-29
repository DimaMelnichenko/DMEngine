#include "DMD3D.h"
#include "Utils\utilites.h"

std::unique_ptr<DMD3D> DMD3D::m_instance;

DMD3D& DMD3D::instance()
{
	if( !m_instance )
		m_instance.reset( new DMD3D() );

	return *m_instance;
}

void DMD3D::destroy()
{
	m_instance.reset();
}


DMD3D::DMD3D(  )
{
	
}

DMD3D::~DMD3D()
{
	Shutdown();
}

bool DMD3D::Initialize( const Config& config, HWND hwnd )
{
	m_screenHeight = (uint32_t)config.screenHeight();
	m_screenWidth = (uint32_t)config.screenWidth();
	m_hWnd = hwnd;
	m_vsync_enabled = config.vSync();

	HRESULT result;
	IDXGIFactory* factory;
	IDXGIAdapter* adapter;
	IDXGIOutput* adapterOutput;
	unsigned int numModes = 0;
	DXGI_MODE_DESC* displayModeList;
	DXGI_ADAPTER_DESC adapterDesc;
	int error;

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
	for( uint16_t i = 0; i < numModes; i++ )
	{
		if( displayModeList[i].Width == m_screenWidth )
		{
			if( displayModeList[i].Height == m_screenHeight )
			{
				m_numerator = displayModeList[i].RefreshRate.Numerator;
				m_denominator = displayModeList[i].RefreshRate.Denominator;
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
	size_t stringLength = 0;
	error = wcstombs_s( &stringLength, m_videoCardDescription, 128, adapterDesc.Description, 128 );
	if( error != 0 )
	{
		return false;
	}

	// Release the display mode list.
	delete[] displayModeList;
	displayModeList = 0;

	// Release the adapter output.
	adapterOutput->Release();
	adapterOutput = 0;

	// Release the adapter.
	adapter->Release();
	adapter = 0;

	// Release the factory.
	factory->Release();
	factory = 0;

	if( !createDeviceSwapChain( hwnd, config.fullScreen() ) )
		return false;

	if( !createRenderTargetView() )
		return false;

	if( !createDepthStencilBufferAndView() )
		return false;

	if( !createRasterDescs() )
		return false;

	if( !createViewport() )
		return false;

	if( !createBlendStates() )
		return false;

	return true;
}

bool DMD3D::createDeviceSwapChain( HWND hwnd, bool fullscreen )
{
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	

	// Initialize the swap chain description.
	ZeroMemory( &swapChainDesc, sizeof( DXGI_SWAP_CHAIN_DESC ) );

	// Set to a single back buffer.
	swapChainDesc.BufferCount = 1;

	// Set the width and height of the back buffer.
	swapChainDesc.BufferDesc.Width = m_screenWidth;
	swapChainDesc.BufferDesc.Height = m_screenHeight;

	// Set regular 32-bit surface for the back buffer.
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// Set the refresh rate of the back buffer.
	if( m_vsync_enabled )
	{
		swapChainDesc.BufferDesc.RefreshRate.Numerator = m_numerator;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = m_denominator;
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
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_1;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	IDXGISwapChain* swapChain;
	ID3D11Device* device;
	ID3D11DeviceContext* deviceContext;
	HRESULT result = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, &featureLevel, 1,
													D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, NULL, &deviceContext );
	if( FAILED( result ) )
	{
		featureLevel = D3D_FEATURE_LEVEL_11_0;
		result = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, &featureLevel, 1,
												D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, NULL, &deviceContext );
		if( FAILED( result ) )
			return false;
	}

	m_swapChain = make_com_ptr<IDXGISwapChain>( swapChain );
	m_device = make_com_ptr<ID3D11Device>( device );
	m_deviceContext = make_com_ptr<ID3D11DeviceContext>( deviceContext );

	return true;
}

bool DMD3D::createRenderTargetView()
{
	ID3D11Texture2D* backBufferPtr;

	// Get the pointer to the back buffer.
	HRESULT result = m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&backBufferPtr );
	if( FAILED( result ) )
	{
		return false;
	}

	// Create the render target view with the back buffer pointer.
	ID3D11RenderTargetView* renderTargetView;
	result = m_device->CreateRenderTargetView( backBufferPtr, NULL, &renderTargetView );
	if( FAILED( result ) )
	{
		return false;
	}

	m_renderTargetView = make_com_ptr<ID3D11RenderTargetView>( renderTargetView );

	// Release pointer to the back buffer as we no longer need it.
	backBufferPtr->Release();
	backBufferPtr = 0;

	return true;
}

bool DMD3D::createDepthStencilBufferAndView()
{
	D3D11_TEXTURE2D_DESC depthBufferDesc;

	// Initialize the description of the depth buffer.
	ZeroMemory( &depthBufferDesc, sizeof( depthBufferDesc ) );

	// Set up the description of the depth buffer.
	depthBufferDesc.Width = m_screenWidth;
	depthBufferDesc.Height = m_screenHeight;
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
	ID3D11Texture2D* depthStencilBuffer;
	HRESULT result = m_device->CreateTexture2D( &depthBufferDesc, NULL, &depthStencilBuffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthStencilBuffer = make_com_ptr<ID3D11Texture2D>( depthStencilBuffer );




	// Initialize the description of the stencil state.
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
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
	ID3D11DepthStencilState* depthStencilState;
	result = m_device->CreateDepthStencilState( &depthStencilDesc, &depthStencilState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthStencilState = make_com_ptr<ID3D11DepthStencilState>( depthStencilState );

	// depth disable state
	depthStencilDesc.DepthEnable = false;
	result = m_device->CreateDepthStencilState( &depthStencilDesc, &depthStencilState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthDisabledStencilState = make_com_ptr<ID3D11DepthStencilState>( depthStencilState );

	// Set the depth stencil state.
	m_deviceContext->OMSetDepthStencilState( m_depthStencilState.get(), 1 );
	//m_deviceContext->OMSetDepthStencilState( nullptr, 0 );


	// Initailze the depth stencil view.
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory( &depthStencilViewDesc, sizeof( depthStencilViewDesc ) );

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	ID3D11DepthStencilView* depthStencilView;
	result = m_device->CreateDepthStencilView( m_depthStencilBuffer.get(), &depthStencilViewDesc, &depthStencilView );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthStencilView = make_com_ptr<ID3D11DepthStencilView>( depthStencilView );

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	ID3D11RenderTargetView* rtv = m_renderTargetView.get();
	m_deviceContext->OMSetRenderTargets( 1, &rtv, m_depthStencilView.get() );

	return true;
}

bool DMD3D::createRasterizerState( D3D11_RASTERIZER_DESC& desc, com_unique_ptr<ID3D11RasterizerState>& state )
{
	ID3D11RasterizerState* rasterState;
	HRESULT result = m_device->CreateRasterizerState( &desc, &rasterState );
	if( FAILED( result ) )
	{
		return false;
	}

	state = make_com_ptr<ID3D11RasterizerState>( rasterState );

	return true;
}

bool DMD3D::createRasterDescs()
{
	// Setup the raster description which will determine how and what polygons will be drawn.
	D3D11_RASTERIZER_DESC rasterDesc;
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


	if( !createRasterizerState( rasterDesc, m_rasterState ) )
		return false;

	// Now set the rasterizer state.
	m_deviceContext->RSSetState( m_rasterState.get() );


	// create back face culling state
	rasterDesc.CullMode = D3D11_CULL_FRONT;
	if( !createRasterizerState( rasterDesc, m_rasterStateFrontCulling ) )
		return false;

	// create shadow render state
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = 0;
	rasterDesc.SlopeScaledDepthBias = 5.5f;
	if( !createRasterizerState( rasterDesc, m_rasterStateShadow ) )
		return false;


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
	if( !createRasterizerState( rasterDesc, m_rasterStateNoCulling ) )
		return false;

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
	if( !createRasterizerState( rasterDesc, m_rasterStateWireframe ) )
		return false;

	return true;
}

bool DMD3D::createViewport()
{
	// Setup the viewport for rendering.
	m_viewport.Width = (float)m_screenWidth;
	m_viewport.Height = (float)m_screenHeight;
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;

	// Create the viewport.
	m_deviceContext->RSSetViewports( 1, &m_viewport );

	return true;
}

bool DMD3D::createBlendState( D3D11_BLEND_DESC& desc, com_unique_ptr<ID3D11BlendState>& state )
{
	ID3D11BlendState* blendState;
	HRESULT result = m_device->CreateBlendState( &desc, &blendState );
	if( FAILED( result ) )
	{
		return false;
	}

	state = make_com_ptr<ID3D11BlendState>( blendState );

	return true;
}

bool DMD3D::createBlendStates()
{

	// Clear the blend state description.
	D3D11_BLEND_DESC blendStateDescription;
	ZeroMemory( &blendStateDescription, sizeof( D3D11_BLEND_DESC ) );

	// Create an alpha enabled blend state description.
	blendStateDescription.AlphaToCoverageEnable = true;	
	blendStateDescription.RenderTarget[0].BlendEnable = TRUE;
	blendStateDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;	
	blendStateDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].RenderTargetWriteMask = 0X0f;
	

	if( !createBlendState( blendStateDescription, m_alphaEnableBlendingState ) )
		return false;

	// Modify the description to create an alpha disabled blend state description.
	blendStateDescription.AlphaToCoverageEnable = false;
	blendStateDescription.RenderTarget[0].BlendEnable = FALSE;
	if( !createBlendState( blendStateDescription, m_alphaDisableBlendingState ) )
		return false;

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
	if( !createBlendState( blendStateDescription, m_blendStateTransparency ) )
		return false;

	return true;
}

void DMD3D::Shutdown( )
{
	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.

	if( m_swapChain )
	{
		m_swapChain->SetFullscreenState( false, NULL );		
	}
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
	m_deviceContext->OMSetBlendState( m_alphaEnableBlendingState.get(), blendFactor, 0xffffffff );

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
	m_deviceContext->OMSetBlendState( m_alphaDisableBlendingState.get(), blendFactor, 0xffffffff );

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
	m_deviceContext->ClearRenderTargetView( m_renderTargetView.get(), color );

	// Clear the depth buffer.
	m_deviceContext->ClearDepthStencilView( m_depthStencilView.get(), D3D11_CLEAR_DEPTH, 1.0f, 0 );

	return;
}

void DMD3D::EndScene( )
{
	// Present the back buffer to the screen since rendering is complete.
	if( m_vsync_enabled )
	{
		// Lock to screen refresh rate.
		m_swapChain->Present ( 1, 0 );
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
	return m_device.get();
}

ID3D11DeviceContext* DMD3D::GetDeviceContext( )
{
	return m_deviceContext.get();
}

void DMD3D::GetVideoCardInfo( char* cardName, int& memory )
{
	strcpy_s( cardName, 128, m_videoCardDescription );
	memory = m_videoCardMemory;
	return;
}

void DMD3D::TurnZBufferOn( )
{
	m_deviceContext->OMSetDepthStencilState( m_depthStencilState.get(), 1 );
	return;
}

void DMD3D::TurnZBufferOff( )
{
	m_deviceContext->OMSetDepthStencilState( m_depthDisabledStencilState.get(), 1 );
	return;
}

ID3D11DepthStencilView* DMD3D::GetDepthStencilView( )
{
	return m_depthStencilView.get();
}

void DMD3D::SetBackBufferRenderTarget( )
{
	// Bind the render target view and depth stencil buffer to the output render pipeline.
	ID3D11RenderTargetView* renderTargetView = m_renderTargetView.get();
	m_deviceContext->OMSetRenderTargets( 1, &renderTargetView, m_depthStencilView.get() );

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
	m_deviceContext->RSSetState( m_rasterState.get() );

	return;
}

void DMD3D::TurnShadowRS()
{
	// Set the culling rasterizer state.
	m_deviceContext->RSSetState( m_rasterStateShadow.get() );

	return;
}

void DMD3D::TurnBackFacesRS()
{
	// Set the culling rasterizer state.
	m_deviceContext->RSSetState( m_rasterStateFrontCulling.get());

	return;
}

void DMD3D::TurnCullingNoneRS( )
{
	// Set the no back face culling rasterizer state.
	m_deviceContext->RSSetState( m_rasterStateNoCulling.get() );

	return;
}

void DMD3D::TurnOnWireframe()
{
	m_deviceContext->RSSetState( m_rasterStateWireframe.get() );
}

void DMD3D::TurnOffWireframe()
{
	m_deviceContext->RSSetState( m_rasterState.get() );
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
	m_deviceContext->OMSetBlendState( m_blendStateTransparency.get(), blendFactor, 0xffffffff );

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
	param_buffer_desc.ByteWidth = (UINT)byte_size;
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

bool DMD3D::createSRV( const com_unique_ptr<ID3D11Buffer>& buffer, D3D11_SHADER_RESOURCE_VIEW_DESC& desc, com_unique_ptr<ID3D11ShaderResourceView>& srv )
{
	ID3D11ShaderResourceView* raw_srv;
	if( FAILED( m_device->CreateShaderResourceView( buffer.get(), &desc, &raw_srv ) ) )
	{
		return false;
	}

	srv.reset( raw_srv );

	return true;
}

bool DMD3D::createUAV( const com_unique_ptr<ID3D11Buffer>& buffer, D3D11_UNORDERED_ACCESS_VIEW_DESC& desc, com_unique_ptr<ID3D11UnorderedAccessView>& uav )
{
	ID3D11UnorderedAccessView* raw_uav;
	if( FAILED( m_device->CreateUnorderedAccessView( buffer.get(), &desc, &raw_uav ) ) )
	{
		return false;
	}

	uav.reset( raw_uav );
	return true;
}

bool DMD3D::createVertexBuffer( com_unique_ptr<ID3D11Buffer> &buffer, void* data, size_t sizeInByte )
{
	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	buffer_desc.ByteWidth = (UINT)sizeInByte;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.MiscFlags = 0;
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;

	if( data )
	{
		D3D11_SUBRESOURCE_DATA init_data;
		memset( &init_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );
		init_data.pSysMem = data;

		return CreateBuffer( &buffer_desc, &init_data, buffer );
	}

	return CreateBuffer( &buffer_desc, nullptr, buffer );
}

bool DMD3D::createIndexBuffer( com_unique_ptr<ID3D11Buffer> &buffer, void* data, size_t sizeInByte )
{
	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	buffer_desc.ByteWidth = (UINT)sizeInByte;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.MiscFlags = 0;
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;

	if( data )
	{
		D3D11_SUBRESOURCE_DATA init_data;
		memset( &init_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );
		init_data.pSysMem = data;

		return CreateBuffer( &buffer_desc, &init_data, buffer );
	}
	
	return CreateBuffer( &buffer_desc, nullptr, buffer );
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

void DMD3D::currentRS( com_unique_ptr<ID3D11RasterizerState>& state )
{
	ID3D11RasterizerState* rs = nullptr;
	m_deviceContext->RSGetState( &rs );
	state = make_com_ptr<ID3D11RasterizerState>( rs );
}

void DMD3D::setRS( ID3D11RasterizerState* state )
{
	m_deviceContext->RSSetState( state );
}

bool DMD3D::setConstantBuffer( SRVType type, uint16_t slot, com_unique_ptr<ID3D11Buffer>& buffer )
{
	ID3D11Buffer* cbuffer = buffer.get();

	switch( type )
	{
		case vs:
			GetDeviceContext()->VSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case ps:
			GetDeviceContext()->PSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case gs:
			GetDeviceContext()->GSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case cs:
			GetDeviceContext()->CSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		default:
			return false;
			break;
	}

	return true;
}