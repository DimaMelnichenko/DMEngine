#include "DMD3D.h"
#include "Utils\utilites.h"
#include "Logger\Logger.h"

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

DMD3D::DMD3D()
{
	
}

DMD3D::~DMD3D()
{
	Shutdown();
}

bool DMD3D::Initialize( const Config& config, HWND hwnd )
{
	m_screenWidth = (uint32_t)config.backBufferWidth();
	m_screenHeight = (uint32_t)config.backBufferHeight();
	m_hWnd = hwnd;
	m_vsync_enabled = config.vSync();
	m_MSAACount = config.MSAACount();

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
	result = adapterOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr );
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

	if( !createSceneTarget() )
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

	// Задний буфер без MSAA: выборки хранит HDR-буфер сцены, а сюда пишет тонмаппинг уже сведённое изображение
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	swapChainDesc.Windowed = !fullscreen;

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	// Discard the back buffer contents after presenting.
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// Don't set the advanced flags.
	swapChainDesc.Flags = 0;

	D3D_FEATURE_LEVEL featureLevel;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	IDXGISwapChain* swapChain;
	ID3D11Device* device;
	ID3D11DeviceContext* deviceContext;

	// Try DirectX 11.1 first, then 11.0.
	auto createDevice = [&]( UINT flags )
	{
		featureLevel = D3D_FEATURE_LEVEL_11_1;
		HRESULT hr = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &featureLevel, 1,
													D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, nullptr, &deviceContext );
		if( FAILED( hr ) )
		{
			featureLevel = D3D_FEATURE_LEVEL_11_0;
			hr = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &featureLevel, 1,
												D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, nullptr, &deviceContext );
		}
		return hr;
	};

#ifdef _DEBUG
	// Debug-слой только в Debug-сборке: он заметно замедляет отрисовку. Нужен компонент Windows «Средства графики»
	HRESULT result = createDevice( D3D11_CREATE_DEVICE_DEBUG );
	if( FAILED( result ) )
	{
		LOG( "D3D11 debug layer is unavailable, creating device without it" );
		result = createDevice( 0 );
	}
#else
	HRESULT result = createDevice( 0 );
#endif
	if( FAILED( result ) )
		return false;

	m_swapChain = make_com_ptr<IDXGISwapChain>( swapChain );
	m_device = make_com_ptr<ID3D11Device>( device );
	m_deviceContext = make_com_ptr<ID3D11DeviceContext>( deviceContext );

	ID3D11InfoQueue* infoQueue = nullptr;
	if( SUCCEEDED( device->QueryInterface( __uuidof( ID3D11InfoQueue ), reinterpret_cast<void**>( &infoQueue ) ) ) )
	{
		m_infoQueue.reset( infoQueue );
		LOG( "D3D11 debug layer is enabled, its messages are written to this log" );
	}

	return true;
}

void DMD3D::logDebugMessages()
{
	if( !m_infoQueue )
		return;

	// Одно и то же сообщение обычно повторяется каждый кадр: пишется только первые maxRepeats раз
	const uint32_t maxRepeats = 3;
	const UINT64 count = m_infoQueue->GetNumStoredMessages();
	for( UINT64 i = 0; i < count; ++i )
	{
		SIZE_T length = 0;
		if( FAILED( m_infoQueue->GetMessage( i, nullptr, &length ) ) || length == 0 )
			continue;

		std::vector<char> buffer( length );
		D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>( buffer.data() );
		if( FAILED( m_infoQueue->GetMessage( i, message, &length ) ) )
			continue;

		if( message->Severity == D3D11_MESSAGE_SEVERITY_INFO || message->Severity == D3D11_MESSAGE_SEVERITY_MESSAGE )
			continue;

		const uint32_t repeats = ++m_debugMessageCounts[message->ID];
		if( repeats > maxRepeats )
			continue;

		const char* severity = message->Severity == D3D11_MESSAGE_SEVERITY_WARNING ? "warning" :
							   message->Severity == D3D11_MESSAGE_SEVERITY_ERROR ? "error" : "corruption";
		LOG( std::string( "D3D11 " ) + severity + ": " + std::string( message->pDescription, message->DescriptionByteLength ? message->DescriptionByteLength - 1 : 0 ) +
			 ( repeats == maxRepeats ? " (further repeats are not logged)" : "" ) );
	}

	m_infoQueue->ClearStoredMessages();
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
	result = m_device->CreateRenderTargetView( backBufferPtr, nullptr, &renderTargetView );
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

bool DMD3D::createSceneTarget()
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = m_screenWidth;
	desc.Height = m_screenHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = m_MSAACount;
	desc.SampleDesc.Quality = m_MSAACount > 1 ? D3D11_STANDARD_MULTISAMPLE_PATTERN : 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | ( m_MSAACount > 1 ? 0 : D3D11_BIND_SHADER_RESOURCE );

	ID3D11Texture2D* texture = nullptr;
	if( FAILED( m_device->CreateTexture2D( &desc, nullptr, &texture ) ) )
		return false;
	m_sceneTexture = make_com_ptr<ID3D11Texture2D>( texture );

	ID3D11RenderTargetView* rtv = nullptr;
	if( FAILED( m_device->CreateRenderTargetView( m_sceneTexture.get(), nullptr, &rtv ) ) )
		return false;
	m_sceneRTV = make_com_ptr<ID3D11RenderTargetView>( rtv );

	// Шейдер читает обычную текстуру: при MSAA выборки сводятся в неё перед тонмаппингом
	ID3D11Texture2D* readable = m_sceneTexture.get();
	if( m_MSAACount > 1 )
	{
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if( FAILED( m_device->CreateTexture2D( &desc, nullptr, &texture ) ) )
			return false;
		m_sceneResolved = make_com_ptr<ID3D11Texture2D>( texture );
		readable = m_sceneResolved.get();
	}

	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( m_device->CreateShaderResourceView( readable, nullptr, &srv ) ) )
		return false;
	m_sceneSRV = make_com_ptr<ID3D11ShaderResourceView>( srv );

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
	depthBufferDesc.SampleDesc.Count = m_MSAACount;
	depthBufferDesc.SampleDesc.Quality = m_MSAACount > 1 ? D3D11_STANDARD_MULTISAMPLE_PATTERN : 0;
	depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthBufferDesc.CPUAccessFlags = 0;
	depthBufferDesc.MiscFlags = 0;

	// Create the texture for the depth buffer using the filled out description.
	ID3D11Texture2D* depthStencilBuffer;
	HRESULT result = m_device->CreateTexture2D( &depthBufferDesc, nullptr, &depthStencilBuffer );
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


	// Initailze the depth stencil view.
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory( &depthStencilViewDesc, sizeof( depthStencilViewDesc ) );

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	if( m_MSAACount > 1 )
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
	else
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

	return true;
}

void DMD3D::Shutdown( )
{
	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.

	if( m_swapChain )
	{
		m_swapChain->SetFullscreenState( false, nullptr );		
	}
}

void DMD3D::setState( RasterState state )
{
	switch( state )
	{
		case RasterState::solid:
			m_deviceContext->RSSetState( m_rasterState.get() );
			break;
		case RasterState::frontCulling:
			m_deviceContext->RSSetState( m_rasterStateFrontCulling.get() );
			break;
		case RasterState::noCulling:
			m_deviceContext->RSSetState( m_rasterStateNoCulling.get() );
			break;
		case RasterState::wireframe:
			m_deviceContext->RSSetState( m_rasterStateWireframe.get() );
			break;
	}

	m_renderState.raster = state;
}

void DMD3D::setState( DepthState state )
{
	ID3D11DepthStencilState* depthState = state == DepthState::enabled ? m_depthStencilState.get() : m_depthDisabledStencilState.get();
	m_deviceContext->OMSetDepthStencilState( depthState, 1 );

	m_renderState.depth = state;
}

void DMD3D::setState( BlendState state )
{
	const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	ID3D11BlendState* blendState = state == BlendState::alpha ? m_alphaEnableBlendingState.get() : m_alphaDisableBlendingState.get();
	m_deviceContext->OMSetBlendState( blendState, blendFactor, 0xffffffff );

	m_renderState.blend = state;
}

void DMD3D::setRenderState( const RenderState& state )
{
	setState( state.raster );
	setState( state.depth );
	setState( state.blend );
}

const RenderState& DMD3D::renderState() const
{
	return m_renderState;
}

void DMD3D::BeginScene( float red, float green, float blue, float alpha )
{
	float color[4];


	// Setup the color to clear the buffer to.
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;

	// Сцена рисуется в HDR-буфер; цвет очистки — линейный, как всё в нём
	ID3D11RenderTargetView* rtv = m_sceneRTV.get();
	m_deviceContext->OMSetRenderTargets( 1, &rtv, m_depthStencilView.get() );
	m_deviceContext->ClearRenderTargetView( m_sceneRTV.get(), color );
	m_deviceContext->ClearDepthStencilView( m_depthStencilView.get(), D3D11_CLEAR_DEPTH, 1.0f, 0 );
}

void DMD3D::setBackBufferTarget()
{
	// Без буфера глубины: тонмаппинг и GUI рисуются поверх всего экрана
	ID3D11RenderTargetView* rtv = m_renderTargetView.get();
	m_deviceContext->OMSetRenderTargets( 1, &rtv, nullptr );
}

const com_unique_ptr<ID3D11ShaderResourceView>& DMD3D::sceneColor()
{
	if( m_sceneResolved )
		m_deviceContext->ResolveSubresource( m_sceneResolved.get(), 0, m_sceneTexture.get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT );

	return m_sceneSRV;
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
	ID3D11ShaderResourceView* views[50] = {};
	m_deviceContext->VSSetShaderResources( 0, 50, views );
	m_deviceContext->GSSetShaderResources( 0, 50, views );
	m_deviceContext->PSSetShaderResources( 0, 50, views );

	logDebugMessages();
}

ID3D11Device* DMD3D::GetDevice( )
{
	return m_device.get();
}

ID3D11DeviceContext* DMD3D::GetDeviceContext( )
{
	return m_deviceContext.get();
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

bool DMD3D::setConstantBuffer( SRVType type, uint16_t slot, com_unique_ptr<ID3D11Buffer>& buffer )
{
	ID3D11Buffer* cbuffer = buffer.get();

	switch( type )
	{
		case SRVType::vs:
			GetDeviceContext()->VSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::ps:
			GetDeviceContext()->PSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::hs:
			GetDeviceContext()->HSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::ds:
			GetDeviceContext()->DSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::gs:
			GetDeviceContext()->GSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::cs:
			GetDeviceContext()->CSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		default:
			return false;
			break;
	}

	return true;
}

void DMD3D::setSRV( SRVType type, uint16_t slot, const com_unique_ptr<ID3D11ShaderResourceView>& srv )
{
	ID3D11ShaderResourceView* const rawSRV = srv.get();
	switch( type )
	{
		case SRVType::vs:
			DMD3D::instance().GetDeviceContext()->VSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::ps:
			DMD3D::instance().GetDeviceContext()->PSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::hs:
			DMD3D::instance().GetDeviceContext()->HSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::ds:
			DMD3D::instance().GetDeviceContext()->DSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::gs:
			DMD3D::instance().GetDeviceContext()->GSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::cs:
			DMD3D::instance().GetDeviceContext()->CSSetShaderResources( slot, 1, &rawSRV );
			break;
		default:
			break;
	}
	
}

#include "ScreenGrab.h"
#include <wincodecsdk.h>

bool DMD3D::createScreenshot()
{
	ID3D11Texture2D* backBuffer = nullptr;
	HRESULT hr = m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );
	if( SUCCEEDED( hr ) )
	{
		std::wstring fileName = L"screenshot" + std::to_wstring( m_screenshotCounter++ ) + L".jpg";
		hr = SaveWICTextureToFile( m_deviceContext.get(), backBuffer, GUID_ContainerFormatJpeg, fileName.data() );
	}

	if( backBuffer )
	{
		backBuffer->Release();
		backBuffer = nullptr;
	}

	return true;
}