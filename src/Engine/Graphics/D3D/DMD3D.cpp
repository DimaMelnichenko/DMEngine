#include "DMD3D.h"
#include "Shaders\slots.h"
#include "Utils\utilites.h"
#include "Logger\Logger.h"
#include <algorithm>
#include <string>
#include <vector>

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

	// Видеокарта: на машине с двумя GPU адаптер 0 — обычно встроенная, движку нужна дискретная. Выбор — как
	// «Высокая производительность» в настройках графики Windows: IDXGIFactory6::EnumAdapterByGpuPreference;
	// программные адаптеры (Microsoft Basic Render Driver) пропускаются. Без IDXGIFactory6 — адаптер 0, как раньше
	IDXGIFactory1* factoryPtr = nullptr;
	if( FAILED( CreateDXGIFactory1( __uuidof( IDXGIFactory1 ), (void**)&factoryPtr ) ) )
		return false;
	auto factory = make_com_ptr<IDXGIFactory1>( factoryPtr );

	IDXGIFactory6* factory6Ptr = nullptr;
	if( SUCCEEDED( factory->QueryInterface( __uuidof( IDXGIFactory6 ), (void**)&factory6Ptr ) ) )
	{
		auto factory6 = make_com_ptr<IDXGIFactory6>( factory6Ptr );
		for( UINT i = 0; ; ++i )
		{
			IDXGIAdapter1* adapterPtr = nullptr;
			if( FAILED( factory6->EnumAdapterByGpuPreference( i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof( IDXGIAdapter1 ),
																  (void**)&adapterPtr ) ) )
				break;
			auto candidate = make_com_ptr<IDXGIAdapter1>( adapterPtr );
			DXGI_ADAPTER_DESC1 desc;
			if( SUCCEEDED( candidate->GetDesc1( &desc ) ) && !( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) )
			{
				m_adapter = std::move( candidate );
				break;
			}
		}
	}
	if( !m_adapter )
	{
		IDXGIAdapter1* adapterPtr = nullptr;
		if( FAILED( factory->EnumAdapters1( 0, &adapterPtr ) ) )
			return false;
		m_adapter = make_com_ptr<IDXGIAdapter1>( adapterPtr );
	}

	DXGI_ADAPTER_DESC1 adapterDesc;
	if( FAILED( m_adapter->GetDesc1( &adapterDesc ) ) )
		return false;
	m_videoCardMemory = (int)( adapterDesc.DedicatedVideoMemory / 1024 / 1024 );
	size_t stringLength = 0;
	if( wcstombs_s( &stringLength, m_videoCardDescription, sizeof( m_videoCardDescription ), adapterDesc.Description, _TRUNCATE ) != 0 )
		return false;
	LOG( std::string( "Video adapter: " ) + m_videoCardDescription + ", dedicated memory " + std::to_string( m_videoCardMemory ) + " MB" );

	// Частота обновления для vsync — по режимам первого выхода адаптера. У карты без своих выходов (монитор
	// подключён к другой) выходов нет — тогда частота по умолчанию, это не ошибка
	m_numerator = 0;
	m_denominator = 1;
	IDXGIOutput* outputPtr = nullptr;
	if( SUCCEEDED( m_adapter->EnumOutputs( 0, &outputPtr ) ) )
	{
		auto output = make_com_ptr<IDXGIOutput>( outputPtr );
		UINT numModes = 0;
		if( SUCCEEDED( output->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr ) ) && numModes )
		{
			std::vector<DXGI_MODE_DESC> modes( numModes );
			if( SUCCEEDED( output->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, modes.data() ) ) )
			{
				for( const DXGI_MODE_DESC& mode : modes )
				{
					if( mode.Width == m_screenWidth && mode.Height == m_screenHeight )
					{
						m_numerator = mode.RefreshRate.Numerator;
						m_denominator = mode.RefreshRate.Denominator;
					}
				}
			}
		}
	}

	if( !createDeviceSwapChain( hwnd, config.fullScreen() ) )
		return false;

	if( !createRenderTargetView() )
		return false;

	if( !createSceneTarget() )
		return false;

	if( !createDepthBuffer() || !createDepthStates() )
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
	// Устройство — на выбранной видеокарте (m_adapter, поэтому D3D_DRIVER_TYPE_UNKNOWN), уровень 11.1 без отката:
	// кольцу констант нужны привязка со смещением и Map( NO_OVERWRITE ) у константных буферов (Windows 10 / 11)
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* deviceContext = nullptr;
	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
	auto createDevice = [&]( UINT flags )
	{
		return D3D11CreateDevice( m_adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, &featureLevel, 1,
								  D3D11_SDK_VERSION, &device, nullptr, &deviceContext );
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
	{
		LOG( "D3D 11.1 device is not available" );
		return false;
	}

	m_device = make_com_ptr<ID3D11Device>( device );
	m_deviceContext = make_com_ptr<ID3D11DeviceContext>( deviceContext );

	ID3D11DeviceContext1* context1 = nullptr;
	if( FAILED( m_deviceContext->QueryInterface( __uuidof( ID3D11DeviceContext1 ), reinterpret_cast<void**>( &context1 ) ) ) )
	{
		LOG( "D3D 11.1 device context is required (constant buffer offsets)" );
		return false;
	}
	m_deviceContext1 = make_com_ptr<ID3D11DeviceContext1>( context1 );
	D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
	if( FAILED( m_device->CheckFeatureSupport( D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof( options ) ) ) ||
		!options.ConstantBufferOffsetting || !options.MapNoOverwriteOnDynamicConstantBuffer )
	{
		LOG( "Constant buffer offsetting and Map( NO_OVERWRITE ) on constant buffers are required" );
		return false;
	}
	if( !m_constantRing.initialize( m_device.get(), m_deviceContext1.get(), constantRingBytes ) )
		return false;

	// Swap chain — flip model (DXGI_SWAP_EFFECT_FLIP_DISCARD), единственная модель D3D12 и рекомендованная DXGI для D3D11:
	// кадр отдаётся композитору без копирования, задних буферов два. Формат заднего буфера в flip model — только UNORM,
	// перевод в sRGB делает вид заднего буфера (createRenderTargetView). Без vsync — DXGI_PRESENT_ALLOW_TEARING, иначе
	// flip model упёрлась бы в частоту монитора. Waitable object — кадр начинается, когда очередь кадров короче предела
	// (waitForNextFrame). Оба флага — только в окне: в исключительном полноэкранном режиме DXGI их не принимает
	IDXGIFactory2* factoryPtr = nullptr;
	if( FAILED( m_adapter->GetParent( __uuidof( IDXGIFactory2 ), (void**)&factoryPtr ) ) )
		return false;
	auto factory = make_com_ptr<IDXGIFactory2>( factoryPtr );

	const bool windowed = !fullscreen;
	m_allowTearing = false;
	IDXGIFactory5* factory5Ptr = nullptr;
	if( windowed && SUCCEEDED( factory->QueryInterface( __uuidof( IDXGIFactory5 ), (void**)&factory5Ptr ) ) )
	{
		auto factory5 = make_com_ptr<IDXGIFactory5>( factory5Ptr );
		BOOL allowTearing = FALSE;
		if( SUCCEEDED( factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof( allowTearing ) ) ) )
			m_allowTearing = allowTearing != FALSE;
	}
	m_swapChainFlags = 0;
	if( windowed )
		m_swapChainFlags = ( m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0 ) | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = m_screenWidth;
	desc.Height = m_screenHeight;
	desc.Format = backBufferFormat;
	// Задний буфер без MSAA: выборки хранит HDR-буфер сцены, а сюда пишет тонмаппинг уже сведённое изображение
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = backBufferCount;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	desc.Flags = m_swapChainFlags;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
	fullscreenDesc.RefreshRate.Numerator = m_vsync_enabled ? m_numerator : 0;
	fullscreenDesc.RefreshRate.Denominator = m_vsync_enabled ? m_denominator : 1;
	fullscreenDesc.Windowed = windowed;

	IDXGISwapChain1* swapChain = nullptr;
	if( FAILED( factory->CreateSwapChainForHwnd( m_device.get(), hwnd, &desc, &fullscreenDesc, nullptr, &swapChain ) ) )
		return false;
	m_swapChain = make_com_ptr<IDXGISwapChain1>( swapChain );
	// Alt+Enter не переключает полноэкранный режим сам: размер целей меняет только WM_SIZE
	factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER );

	if( m_swapChainFlags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT )
	{
		IDXGISwapChain2* swapChain2Ptr = nullptr;
		if( SUCCEEDED( m_swapChain->QueryInterface( __uuidof( IDXGISwapChain2 ), (void**)&swapChain2Ptr ) ) )
		{
			auto swapChain2 = make_com_ptr<IDXGISwapChain2>( swapChain2Ptr );
			// Два кадра в очереди: CPU готовит следующий, пока GPU рисует текущий; с одним задержка меньше, но GPU простаивает
			swapChain2->SetMaximumFrameLatency( 2 );
			m_frameLatencyWaitable = swapChain2->GetFrameLatencyWaitableObject();
		}
	}

	LOG( std::string( "Swap chain: flip model, " ) + std::to_string( backBufferCount ) + " buffers, feature level 11.1" +
		 ( m_allowTearing ? ", tearing allowed" : ", no tearing" ) + ( m_frameLatencyWaitable ? ", frame latency waitable object" : "" ) +
		 ", constant ring " + std::to_string( constantRingBytes / 1024 ) + " KB" );

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

	// Задний буфер flip model — UNORM; вид — sRGB: тонмаппинг и GUI пишут линейный цвет, перевод делает оборудование,
	// байты в буфере те же, что были у заднего буфера R8G8B8A8_UNORM_SRGB
	D3D11_RENDER_TARGET_VIEW_DESC viewDesc = {};
	viewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	ID3D11RenderTargetView* renderTargetView;
	result = m_device->CreateRenderTargetView( backBufferPtr, &viewDesc, &renderTargetView );
	if( FAILED( result ) )
	{
		return false;
	}

	m_backBufferTarget.reset( renderTargetView, backBufferPtr );

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
	m_sceneTarget.reset( rtv, m_sceneTexture.get() );

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
	m_sceneSRV.reset( srv, readable );

	return true;
}

bool DMD3D::createDepthBuffer()
{
	D3D11_TEXTURE2D_DESC depthBufferDesc;

	// Initialize the description of the depth buffer.
	ZeroMemory( &depthBufferDesc, sizeof( depthBufferDesc ) );

	// Set up the description of the depth buffer.
	depthBufferDesc.Width = m_screenWidth;
	depthBufferDesc.Height = m_screenHeight;
	depthBufferDesc.MipLevels = 1;
	depthBufferDesc.ArraySize = 1;
	// Reversed-Z: float-глубина, 1 у ближней плоскости (DMCamera); трафарет не используется
	depthBufferDesc.Format = DXGI_FORMAT_D32_FLOAT;
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

	// Initailze the depth stencil view.
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory( &depthStencilViewDesc, sizeof( depthStencilViewDesc ) );

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
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

	m_sceneDepth.reset( depthStencilView, m_depthStencilBuffer.get() );

	return true;
}

bool DMD3D::createDepthStates()
{
	HRESULT result;

	// Initialize the description of the stencil state.
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	ZeroMemory( &depthStencilDesc, sizeof( depthStencilDesc ) );

	// Set up the description of the stencil state.
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	// Обратная глубина: ближе — больше
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_GREATER;

	depthStencilDesc.StencilEnable = false;
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

	// Только проверка глубины, без записи
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	result = m_device->CreateDepthStencilState( &depthStencilDesc, &depthStencilState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthReadOnlyStencilState = make_com_ptr<ID3D11DepthStencilState>( depthStencilState );

	// Фон на дальней плоскости: глубина 0 проходит там, где буфер глубины остался очищенным («ближе или равно»)
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
	result = m_device->CreateDepthStencilState( &depthStencilDesc, &depthStencilState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthReadOnlyNearOrEqualStencilState = make_com_ptr<ID3D11DepthStencilState>( depthStencilState );

	// Непрозрачные после depth prepass: проходит только поверхность, чья глубина и записана (вариант «только глубина»
	// считает позицию так же, до бита)
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_EQUAL;
	result = m_device->CreateDepthStencilState( &depthStencilDesc, &depthStencilState );
	if( FAILED( result ) )
	{
		return false;
	}

	m_depthReadOnlyEqualStencilState = make_com_ptr<ID3D11DepthStencilState>( depthStencilState );

	// Set the depth stencil state.
	m_deviceContext->OMSetDepthStencilState( m_depthStencilState.get(), 1 );

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

	// Зеркальные меши: лицевые грани — против часовой стрелки
	rasterDesc.FrontCounterClockwise = TRUE;
	if( !createRasterizerState( rasterDesc, m_rasterStateNoCullingMirrored ) )
		return false;
	rasterDesc.CullMode = D3D11_CULL_BACK;
	if( !createRasterizerState( rasterDesc, m_rasterStateSolidMirrored ) )
		return false;

	if( !setShadowSlopeBias( 2.0f ) )
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

	// Альфа-блендинг полупрозрачных. Без alpha-to-coverage: при одной выборке MSAA он отбрасывает пиксели с альфой
	// ниже 0,5 целиком, и полупрозрачное пропадает; вырезанное по альфе (Masked) отсекает шейдер
	blendStateDescription.AlphaToCoverageEnable = false;
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

	// Сложение: цель += источник (уровни bloom)
	blendStateDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	if( !createBlendState( blendStateDescription, m_additiveBlendingState ) )
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

	if( m_frameLatencyWaitable )
	{
		CloseHandle( m_frameLatencyWaitable );
		m_frameLatencyWaitable = nullptr;
	}
	if( m_swapChain )
	{
		m_swapChain->SetFullscreenState( false, nullptr );		
	}
}

ID3D11RasterizerState* DMD3D::rasterObject( RasterState state ) const
{
	switch( state )
	{
		case RasterState::frontCulling: return m_rasterStateFrontCulling.get();
		case RasterState::noCulling: return m_rasterStateNoCulling.get();
		case RasterState::wireframe: return m_rasterStateWireframe.get();
		case RasterState::solidMirrored: return m_rasterStateSolidMirrored.get();
		case RasterState::noCullingMirrored: return m_rasterStateNoCullingMirrored.get();
		case RasterState::csmShadowDepth: return m_rasterStateShadowDepth.get();
		default: return m_rasterState.get();
	}
}

ID3D11DepthStencilState* DMD3D::depthObject( DepthState state ) const
{
	switch( state )
	{
		case DepthState::enabled: return m_depthStencilState.get();
		case DepthState::readOnly: return m_depthReadOnlyStencilState.get();
		case DepthState::readOnlyNearOrEqual: return m_depthReadOnlyNearOrEqualStencilState.get();
		case DepthState::readOnlyEqual: return m_depthReadOnlyEqualStencilState.get();
		default: return m_depthDisabledStencilState.get();
	}
}

ID3D11BlendState* DMD3D::blendObject( BlendState state ) const
{
	switch( state )
	{
		case BlendState::alpha: return m_alphaEnableBlendingState.get();
		case BlendState::additive: return m_additiveBlendingState.get();
		default: return m_alphaDisableBlendingState.get();
	}
}

void DMD3D::setState( RasterState state )
{
	m_renderState.raster = state;
}

void DMD3D::setState( DepthState state )
{
	m_renderState.depth = state;
}

void DMD3D::setState( BlendState state )
{
	m_renderState.blend = state;
}

void DMD3D::setRenderState( const RenderState& state )
{
	m_renderState = state;
}

const RenderState& DMD3D::renderState() const
{
	return m_renderState;
}

const Pipeline& DMD3D::pipeline( const PipelineDesc& desc )
{
	const uint64_t key = desc.key();
	auto found = m_pipelines.find( key );
	if( found != m_pipelines.end() )
		return found->second;

	const uint32_t id = static_cast<uint32_t>( m_pipelines.size() );
	if( m_pipelinesWarm )
	{
		// Не из списка прогрева: в D3D12 сборка PSO в кадре — фриз; дополнить прогрев (DMShader::warmPipelines)
		++m_lazyPipelines;
		LOG( "Pipeline " + std::to_string( id ) + " is created lazily: raster " + std::to_string( static_cast<int>( desc.state.raster ) ) +
			 ", depth " + std::to_string( static_cast<int>( desc.state.depth ) ) + ", blend " + std::to_string( static_cast<int>( desc.state.blend ) ) +
			 ", topology " + std::to_string( static_cast<int>( desc.topology ) ) );
	}
	return m_pipelines.emplace( key, Pipeline( desc, id ) ).first->second;
}

void DMD3D::setPipeline( const Pipeline& pipeline )
{
	const PipelineDesc& desc = pipeline.desc();
	m_deviceContext->IASetInputLayout( desc.layout ? desc.layout->handle() : nullptr );
	setShaderStage( SRVType::vs, desc.vertex );
	setShaderStage( SRVType::gs, desc.geometry );
	setShaderStage( SRVType::hs, desc.hull );
	setShaderStage( SRVType::ds, desc.domain );
	setShaderStage( SRVType::ps, desc.pixel );
	// Объекты состояний берутся по перечислению при каждой привязке: растеризатор теней пересоздаётся (setShadowSlopeBias)
	m_deviceContext->RSSetState( rasterObject( desc.state.raster ) );
	m_deviceContext->OMSetDepthStencilState( depthObject( desc.state.depth ), 1 );
	const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_deviceContext->OMSetBlendState( blendObject( desc.state.blend ), blendFactor, 0xffffffff );
	m_deviceContext->IASetPrimitiveTopology( desc.topology );
	m_renderState = desc.state;
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
	beginPass( PassDesc{ "Scene clear", { { &m_sceneTarget, "scene color" } }, { &m_sceneDepth, "scene depth" }, m_screenWidth, m_screenHeight } );
	m_deviceContext->ClearRenderTargetView( m_sceneTarget.color(), color );
	// Обратная глубина: очищенный буфер — дальняя плоскость, 0
	m_deviceContext->ClearDepthStencilView( m_sceneDepth.depth(), D3D11_CLEAR_DEPTH, 0.0f, 0 );
}

void DMD3D::clearDepth( const TargetView& target, float depth )
{
	if( target.depth() )
		m_deviceContext->ClearDepthStencilView( target.depth(), D3D11_CLEAR_DEPTH, depth, 0 );
}

void DMD3D::clearTarget( const TargetView& target, const float color[4] )
{
	if( target.color() )
		m_deviceContext->ClearRenderTargetView( target.color(), color );
}

bool DMD3D::setShadowSlopeBias( float slopeBias )
{
	// Без отсечения граней: у рельефа (высотное поле) нет граней «к свету», тонкие панели и лепестки тоже отбрасывают
	// тень. Постоянного смещения нет: у D32_FLOAT оно зависит от порядка числа. Глубина обратная — смещение от света
	// уменьшает её, поэтому знак минус. Без отсечения по глубине — объекты перед ближней плоскостью вида света
	// прижимаются к ней, а не пропадают (pancaking)
	D3D11_RASTERIZER_DESC desc = {};
	desc.FillMode = D3D11_FILL_SOLID;
	desc.CullMode = D3D11_CULL_NONE;
	desc.DepthBias = 0;
	desc.SlopeScaledDepthBias = -slopeBias;
	desc.DepthBiasClamp = -0.01f;
	desc.DepthClipEnable = FALSE;
	// В контекст новый объект попадёт со следующим setPipeline (объекты состояний берутся по перечислению)
	return createRasterizerState( desc, m_rasterStateShadowDepth );
}

int DMD3D::stageIndex( SRVType type )
{
	switch( type )
	{
		case SRVType::vs: return 0;
		case SRVType::hs: return 1;
		case SRVType::ds: return 2;
		case SRVType::gs: return 3;
		case SRVType::ps: return 4;
		default: return 5;	// cs
	}
}

void DMD3D::unbindTransientResources( bool all )
{
	ID3D11ShaderResourceView* views[SLOT_TRANSIENT_COUNT] = {};
	static const SRVType stages[stageCount] = { SRVType::vs, SRVType::hs, SRVType::ds, SRVType::gs, SRVType::ps, SRVType::cs };
	for( int stage = 0; stage < stageCount; ++stage )
	{
		const uint16_t count = all ? SLOT_TRANSIENT_COUNT : m_boundTransientEnd[stage];
		if( count == 0 )
			continue;
		switch( stages[stage] )
		{
			case SRVType::vs: m_deviceContext->VSSetShaderResources( 0, count, views ); break;
			case SRVType::hs: m_deviceContext->HSSetShaderResources( 0, count, views ); break;
			case SRVType::ds: m_deviceContext->DSSetShaderResources( 0, count, views ); break;
			case SRVType::gs: m_deviceContext->GSSetShaderResources( 0, count, views ); break;
			case SRVType::ps: m_deviceContext->PSSetShaderResources( 0, count, views ); break;
			// Compute-проходы (расстановка, частицы, небо, воздушная перспектива, экспозиция) тоже: к следующему кадру их
			// SRV столкнулись бы с записью в те же ресурсы
			default: m_deviceContext->CSSetShaderResources( 0, count, views ); break;
		}
		memset( m_boundSRVs[stage], 0, sizeof( ID3D11Resource* ) * count );
		m_boundTransientEnd[stage] = 0;
	}
}

void DMD3D::beginPass( const PassDesc& pass )
{
	// Что проход пишет: цели и UAV. Их виды снимаются со входов всех стадий (иначе D3D11 отвязал бы цель сам, с
	// предупреждением debug-слоя); транзитные слоты — чистые, проход привязывает свои ресурсы после beginPass
	ID3D11Resource* written[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT + 1 + D3D11_1_UAV_SLOT_COUNT] = {};
	uint32_t writtenCount = 0;
	const auto addWritten = [&]( ID3D11Resource* resource )
	{
		if( resource && writtenCount < std::size( written ) )
			written[writtenCount++] = resource;
	};
	for( const PassDesc::Target& target : pass.colors )
		addWritten( target.view ? target.view->resource() : nullptr );
	addWritten( pass.depth.view ? pass.depth.view->resource() : nullptr );
	for( const PassDesc::Write& write : pass.writes )
		addWritten( write.view ? write.view->resource() : nullptr );
	const auto isWritten = [&]( ID3D11Resource* resource )
	{
		for( uint32_t i = 0; i < writtenCount; ++i )
			if( written[i] == resource )
				return true;
		return false;
	};

	unbindTransientResources( false );
	static const SRVType stages[stageCount] = { SRVType::vs, SRVType::hs, SRVType::ds, SRVType::gs, SRVType::ps, SRVType::cs };
	for( int stage = 0; stage < stageCount; ++stage )
	{
		for( uint16_t slot = SLOT_TRANSIENT_COUNT; slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++slot )
		{
			if( m_boundSRVs[stage][slot] && isWritten( m_boundSRVs[stage][slot] ) )
				unbindSRV( stages[stage], slot );
		}
	}
	for( uint16_t slot = 0; slot < D3D11_1_UAV_SLOT_COUNT; ++slot )
	{
		if( m_boundUAVs[slot] )
			unbindUAVs( slot, 1 );
	}

	ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	const uint32_t colorCount = std::min<uint32_t>( static_cast<uint32_t>( pass.colors.size() ), D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT );
	for( uint32_t i = 0; i < colorCount; ++i )
		targets[i] = pass.colors[i].view ? pass.colors[i].view->color() : nullptr;
	m_deviceContext->OMSetRenderTargets( colorCount, colorCount ? targets : nullptr, pass.depth.view ? pass.depth.view->depth() : nullptr );
	m_passTargetCount = 0;
	for( uint32_t i = 0; i < colorCount; ++i )
		if( pass.colors[i].view && pass.colors[i].view->resource() )
			m_passTargets[m_passTargetCount++] = pass.colors[i].view->resource();
	if( pass.depth.view && pass.depth.view->resource() )
		m_passTargets[m_passTargetCount++] = pass.depth.view->resource();
	if( pass.width && pass.height )
	{
		D3D11_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>( pass.width );
		viewport.Height = static_cast<float>( pass.height );
		viewport.MaxDepth = 1.0f;
		m_deviceContext->RSSetViewports( 1, &viewport );
	}

	if( m_recordingPasses )
	{
		std::string record = std::string( pass.name ) + ":";
		for( const PassDesc::Target& target : pass.colors )
			record += std::string( " color[" ) + target.name + "]";
		if( pass.depth.view )
			record += std::string( " depth[" ) + pass.depth.name + "]";
		if( pass.width && pass.height )
			record += " " + std::to_string( pass.width ) + "x" + std::to_string( pass.height );
		if( pass.colors.empty() && !pass.depth.view )
			record += " compute";
		for( const PassDesc::Read& read : pass.reads )
			record += std::string( " reads[" ) + read.name + "]";
		for( const PassDesc::Write& write : pass.writes )
			record += std::string( " writes[" ) + write.name + "]";
		m_passRecords.push_back( record );
	}
}

const ShaderView& DMD3D::sceneColor()
{
	if( m_sceneResolved )
		m_deviceContext->ResolveSubresource( m_sceneResolved.get(), 0, m_sceneTexture.get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT );

	return m_sceneSRV;
}

void DMD3D::EndScene( )
{
	// С vsync — по частоте монитора; без него — сразу, с разрывом кадра (tearing), если DXGI его поддерживает: иначе
	// flip model всё равно ждала бы обновления экрана
	const UINT flags = !m_vsync_enabled && m_allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
	const HRESULT hr = m_swapChain->Present( m_vsync_enabled ? 1 : 0, flags );
	if( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET )
		LOG( "Present: device removed or reset, reason " + std::to_string( static_cast<long>( m_device->GetDeviceRemovedReason() ) ) );


	// Ресурсы сцены (SLOT_LIGHTS и дальше) живут до следующего кадра
	unbindTransientResources( true );

	if( m_recordingPasses )
	{
		m_recordingPasses = false;
		LOG( "Frame passes: " + std::to_string( m_passRecords.size() ) );
		for( size_t i = 0; i < m_passRecords.size(); ++i )
			LOG( "  " + std::to_string( i ) + ". " + m_passRecords[i] );
	}

	logDebugMessages();
}

void DMD3D::waitForNextFrame()
{
	if( m_frameLatencyWaitable )
		WaitForSingleObjectEx( m_frameLatencyWaitable, 1000, TRUE );
}

void DMD3D::waitForGpu()
{
	// D3D11 дождался бы GPU сам внутри ResizeBuffers, но порядок пересоздания целей один для обоих API: ожидание —
	// событие в конце очереди команд, как fence в D3D12
	D3D11_QUERY_DESC desc = {};
	desc.Query = D3D11_QUERY_EVENT;
	ID3D11Query* queryPtr = nullptr;
	if( FAILED( m_device->CreateQuery( &desc, &queryPtr ) ) )
	{
		m_deviceContext->Flush();
		return;
	}
	auto query = make_com_ptr<ID3D11Query>( queryPtr );
	m_deviceContext->End( query.get() );
	m_deviceContext->Flush();
	BOOL done = FALSE;
	while( m_deviceContext->GetData( query.get(), &done, sizeof( done ), 0 ) == S_FALSE )
		SwitchToThread();
}

void DMD3D::releaseSizedTargets()
{
	m_deviceContext->OMSetRenderTargets( 0, nullptr, nullptr );
	m_passTargetCount = 0;
	m_backBufferTarget.reset();
	m_sceneSRV.reset();
	m_sceneTarget.reset();
	m_sceneResolved.reset();
	m_sceneTexture.reset();
	m_sceneDepth.reset();
	m_depthStencilBuffer.reset();
}

bool DMD3D::resize( uint32_t width, uint32_t height )
{
	if( !m_swapChain || width == 0 || height == 0 )
		return false;
	if( width == m_screenWidth && height == m_screenHeight )
		return true;

	// Как в D3D12: GPU закончил с задними буферами → ссылок на них нет → ResizeBuffers → цели заново. Буфер сцены и
	// глубина — того же размера, что задний буфер, поэтому пересоздаются вместе с ним
	unbindTransientResources( true );
	waitForGpu();
	releaseSizedTargets();
	const HRESULT hr = m_swapChain->ResizeBuffers( backBufferCount, width, height, backBufferFormat, m_swapChainFlags );
	if( FAILED( hr ) )
	{
		LOG( "ResizeBuffers " + std::to_string( width ) + "x" + std::to_string( height ) + " failed, HRESULT " + std::to_string( static_cast<long>( hr ) ) );
		return false;
	}
	m_screenWidth = width;
	m_screenHeight = height;
	if( !createRenderTargetView() || !createSceneTarget() || !createDepthBuffer() || !createViewport() )
		return false;
	LOG( "Back buffer resized to " + std::to_string( width ) + "x" + std::to_string( height ) );
	return true;
}

ID3D11Device* DMD3D::GetDevice( )
{
	return m_device.get();
}

ID3D11DeviceContext* DMD3D::GetDeviceContext( )
{
	return m_deviceContext.get();
}

namespace
{

bool isDepthFormat( DXGI_FORMAT format )
{
	return format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D16_UNORM ||
		   format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
}

// Какой вид строить: явный или по описанию текстуры
TextureViewDesc::Kind resolveKind( const TextureDesc& texture, const TextureViewDesc& view )
{
	if( view.kind != TextureViewDesc::Kind::automatic )
		return view.kind;
	if( texture.depth > 1 )
		return TextureViewDesc::Kind::texture3D;
	if( texture.cube )
		return TextureViewDesc::Kind::cube;
	return texture.arraySize > 1 ? TextureViewDesc::Kind::texture2DArray : TextureViewDesc::Kind::texture2D;
}

UINT bindFlags( uint32_t usage )
{
	UINT flags = 0;
	if( usage & TextureUsage::shaderResource )
		flags |= D3D11_BIND_SHADER_RESOURCE;
	if( usage & TextureUsage::renderTarget )
		flags |= D3D11_BIND_RENDER_TARGET;
	if( usage & TextureUsage::depthStencil )
		flags |= D3D11_BIND_DEPTH_STENCIL;
	if( usage & TextureUsage::unorderedAccess )
		flags |= D3D11_BIND_UNORDERED_ACCESS;
	return flags;
}

}

bool DMD3D::createBuffer( const BufferDesc& desc, const void* initialData, Buffer& buffer )
{
	// Константы кадра — участок кольца, своего ресурса нет: данные появятся при первой записи
	if( ( desc.usage & BufferUsage::constant ) && ( desc.usage & BufferUsage::cpuWrite ) )
	{
		buffer.reset( nullptr, desc );
		return true;
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = desc.size;
	bufferDesc.StructureByteStride = ( desc.usage & BufferUsage::structured ) ? desc.stride : 0;
	if( desc.usage & BufferUsage::constant )
		bufferDesc.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
	if( desc.usage & BufferUsage::vertex )
		bufferDesc.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
	if( desc.usage & BufferUsage::index )
		bufferDesc.BindFlags |= D3D11_BIND_INDEX_BUFFER;
	if( desc.usage & BufferUsage::shaderResource )
		bufferDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
	if( desc.usage & BufferUsage::unorderedAccess )
		bufferDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	if( desc.usage & BufferUsage::structured )
		bufferDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	if( desc.usage & BufferUsage::raw )
		bufferDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if( desc.usage & BufferUsage::indirectArgs )
		bufferDesc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	if( desc.usage & BufferUsage::readback )
	{
		bufferDesc.Usage = D3D11_USAGE_STAGING;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		bufferDesc.BindFlags = 0;
	}
	else if( desc.usage & BufferUsage::cpuWrite )
	{
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	}
	else
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;

	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = initialData;
	ID3D11Buffer* raw = nullptr;
	if( FAILED( m_device->CreateBuffer( &bufferDesc, initialData ? &data : nullptr, &raw ) ) )
		return false;
	buffer.reset( raw, desc );
	return true;
}

bool DMD3D::createShaderConstantBuffer( size_t byteSize, Buffer& buffer )
{
	BufferDesc desc;
	desc.size = static_cast<uint32_t>( byteSize );
	desc.usage = BufferUsage::constant | BufferUsage::cpuWrite;
	return createBuffer( desc, nullptr, buffer );
}

void DMD3D::beginFrame()
{
	waitForNextFrame();
	m_constantRing.beginFrame();
	if( m_passLogRequested )
	{
		m_passLogRequested = false;
		m_recordingPasses = true;
		m_passRecords.clear();
	}
}

void* DMD3D::beginConstants( Buffer& buffer, uint32_t size )
{
	uint32_t offset = 0;
	uint32_t bytes = 0;
	void* data = m_constantRing.beginWrite( size, offset, bytes );
	buffer.setRingSlice( offset, data ? bytes : 0 );
	return data;
}

void DMD3D::endConstants()
{
	m_constantRing.finishWrite();
}

bool DMD3D::createVertexBuffer( Buffer& buffer, const void* data, size_t sizeInBytes )
{
	BufferDesc desc;
	desc.size = static_cast<uint32_t>( sizeInBytes );
	desc.usage = BufferUsage::vertex;
	return createBuffer( desc, data, buffer );
}

bool DMD3D::createIndexBuffer( Buffer& buffer, const void* data, size_t sizeInBytes )
{
	BufferDesc desc;
	desc.size = static_cast<uint32_t>( sizeInBytes );
	desc.usage = BufferUsage::index;
	return createBuffer( desc, data, buffer );
}

bool DMD3D::createShaderView( const Buffer& buffer, const BufferViewDesc& desc, ShaderView& view )
{
	// Байтовый вид — R32_TYPELESS по 4 байта (BufferEx с флагом RAW), структурный — без формата, элементы по stride
	const uint32_t elementSize = desc.raw ? 4 : std::max( buffer.desc().stride, 1u );
	const uint32_t count = desc.elementCount ? desc.elementCount : buffer.size() / elementSize - desc.firstElement;
	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	if( desc.raw )
	{
		viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		viewDesc.BufferEx.FirstElement = desc.firstElement;
		viewDesc.BufferEx.NumElements = count;
		viewDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	}
	else
	{
		viewDesc.Format = DXGI_FORMAT_UNKNOWN;
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		viewDesc.Buffer.FirstElement = desc.firstElement;
		viewDesc.Buffer.NumElements = count;
	}
	ID3D11ShaderResourceView* raw = nullptr;
	if( FAILED( m_device->CreateShaderResourceView( buffer.handle(), &viewDesc, &raw ) ) )
		return false;
	view.reset( raw, buffer.handle() );
	return true;
}

bool DMD3D::createStorageView( const Buffer& buffer, const BufferViewDesc& desc, StorageView& view )
{
	const uint32_t elementSize = desc.raw ? 4 : std::max( buffer.desc().stride, 1u );
	const uint32_t count = desc.elementCount ? desc.elementCount : buffer.size() / elementSize - desc.firstElement;
	D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	viewDesc.Buffer.FirstElement = desc.firstElement;
	viewDesc.Buffer.NumElements = count;
	viewDesc.Buffer.Flags = desc.raw ? D3D11_BUFFER_UAV_FLAG_RAW : 0;
	ID3D11UnorderedAccessView* raw = nullptr;
	if( FAILED( m_device->CreateUnorderedAccessView( buffer.handle(), &viewDesc, &raw ) ) )
		return false;
	view.reset( raw, buffer.handle() );
	return true;
}

bool DMD3D::createTexture( const TextureDesc& desc, const TextureData* initial, Texture& texture )
{
	// Подресурсы D3D11 — в том же порядке, что TextureData: срез за срезом, внутри среза мипы
	std::vector<D3D11_SUBRESOURCE_DATA> data;
	if( initial )
	{
		const uint32_t count = desc.arraySize * std::max( desc.mipCount, 1u );
		data.resize( count );
		for( uint32_t i = 0; i < count; ++i )
		{
			data[i].pSysMem = initial[i].data;
			data[i].SysMemPitch = initial[i].rowPitch;
			data[i].SysMemSlicePitch = initial[i].slicePitch;
		}
	}

	TextureDesc created = desc;
	ID3D11Resource* resource = nullptr;
	if( desc.depth > 1 )
	{
		D3D11_TEXTURE3D_DESC textureDesc = {};
		textureDesc.Width = desc.width;
		textureDesc.Height = desc.height;
		textureDesc.Depth = desc.depth;
		textureDesc.MipLevels = desc.mipCount;
		textureDesc.Format = desc.format;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = bindFlags( desc.usage );
		textureDesc.MiscFlags = ( desc.usage & TextureUsage::generateMips ) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
		ID3D11Texture3D* raw = nullptr;
		if( FAILED( m_device->CreateTexture3D( &textureDesc, initial ? data.data() : nullptr, &raw ) ) )
			return false;
		raw->GetDesc( &textureDesc );
		created.mipCount = textureDesc.MipLevels;
		created.arraySize = 1;
		resource = raw;
	}
	else
	{
		D3D11_TEXTURE2D_DESC textureDesc = {};
		textureDesc.Width = desc.width;
		textureDesc.Height = desc.height;
		textureDesc.MipLevels = desc.mipCount;
		textureDesc.ArraySize = desc.arraySize;
		textureDesc.Format = desc.format;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = bindFlags( desc.usage );
		textureDesc.MiscFlags = ( desc.cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0 ) |
								( ( desc.usage & TextureUsage::generateMips ) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0 );
		ID3D11Texture2D* raw = nullptr;
		if( FAILED( m_device->CreateTexture2D( &textureDesc, initial ? data.data() : nullptr, &raw ) ) )
			return false;
		raw->GetDesc( &textureDesc );
		created.mipCount = textureDesc.MipLevels;
		resource = raw;
	}
	texture.reset( resource, created );
	return true;
}

bool DMD3D::createShaderView( const Texture& texture, const TextureViewDesc& desc, ShaderView& view )
{
	const TextureDesc& textureDesc = texture.desc();
	const bool whole = desc.kind == TextureViewDesc::Kind::automatic && desc.format == DXGI_FORMAT_UNKNOWN &&
					   desc.firstMip == 0 && desc.mipCount == 0 && desc.firstSlice == 0 && desc.sliceCount == 0;
	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	const UINT mipCount = desc.mipCount ? desc.mipCount : static_cast<UINT>( -1 );	// -1 — все с firstMip
	const UINT sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	switch( resolveKind( textureDesc, desc ) )
	{
		case TextureViewDesc::Kind::texture3D:
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
			viewDesc.Texture3D.MostDetailedMip = desc.firstMip;
			viewDesc.Texture3D.MipLevels = mipCount;
			break;
		case TextureViewDesc::Kind::cube:
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			viewDesc.TextureCube.MostDetailedMip = desc.firstMip;
			viewDesc.TextureCube.MipLevels = mipCount;
			break;
		case TextureViewDesc::Kind::texture2DArray:
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			viewDesc.Texture2DArray.MostDetailedMip = desc.firstMip;
			viewDesc.Texture2DArray.MipLevels = mipCount;
			viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
			viewDesc.Texture2DArray.ArraySize = sliceCount;
			break;
		default:
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MostDetailedMip = desc.firstMip;
			viewDesc.Texture2D.MipLevels = mipCount;
			break;
	}
	ID3D11ShaderResourceView* raw = nullptr;
	// Вид на всю текстуру как есть — по описанию ресурса (так же работает и для MSAA)
	if( FAILED( m_device->CreateShaderResourceView( texture.handle(), whole ? nullptr : &viewDesc, &raw ) ) )
		return false;
	view.reset( raw, texture.handle() );
	return true;
}

bool DMD3D::createTargetView( const Texture& texture, const TextureViewDesc& desc, TargetView& view )
{
	const TextureDesc& textureDesc = texture.desc();
	const DXGI_FORMAT format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	const bool array = kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube;
	const UINT sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	if( isDepthFormat( format ) )
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC depthDesc = {};
		depthDesc.Format = format;
		if( array )
		{
			depthDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			depthDesc.Texture2DArray.MipSlice = desc.firstMip;
			depthDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
			depthDesc.Texture2DArray.ArraySize = sliceCount;
		}
		else
		{
			depthDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			depthDesc.Texture2D.MipSlice = desc.firstMip;
		}
		ID3D11DepthStencilView* raw = nullptr;
		if( FAILED( m_device->CreateDepthStencilView( texture.handle(), &depthDesc, &raw ) ) )
			return false;
		view.reset( raw, texture.handle() );
		return true;
	}

	D3D11_RENDER_TARGET_VIEW_DESC colorDesc = {};
	colorDesc.Format = format;
	if( kind == TextureViewDesc::Kind::texture3D )
	{
		colorDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
		colorDesc.Texture3D.MipSlice = desc.firstMip;
		colorDesc.Texture3D.FirstWSlice = desc.firstSlice;
		colorDesc.Texture3D.WSize = desc.sliceCount ? desc.sliceCount : static_cast<UINT>( -1 );
	}
	else if( array )
	{
		colorDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		colorDesc.Texture2DArray.MipSlice = desc.firstMip;
		colorDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
		colorDesc.Texture2DArray.ArraySize = sliceCount;
	}
	else
	{
		colorDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		colorDesc.Texture2D.MipSlice = desc.firstMip;
	}
	ID3D11RenderTargetView* raw = nullptr;
	if( FAILED( m_device->CreateRenderTargetView( texture.handle(), &colorDesc, &raw ) ) )
		return false;
	view.reset( raw, texture.handle() );
	return true;
}

bool DMD3D::createStorageView( const Texture& texture, const TextureViewDesc& desc, StorageView& view )
{
	const TextureDesc& textureDesc = texture.desc();
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	if( kind == TextureViewDesc::Kind::texture3D )
	{
		viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		viewDesc.Texture3D.MipSlice = desc.firstMip;
		viewDesc.Texture3D.FirstWSlice = desc.firstSlice;
		viewDesc.Texture3D.WSize = desc.sliceCount ? desc.sliceCount : static_cast<UINT>( -1 );
	}
	else if( kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube )
	{
		viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		viewDesc.Texture2DArray.MipSlice = desc.firstMip;
		viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
		viewDesc.Texture2DArray.ArraySize = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	}
	else
	{
		viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = desc.firstMip;
	}
	ID3D11UnorderedAccessView* raw = nullptr;
	if( FAILED( m_device->CreateUnorderedAccessView( texture.handle(), &viewDesc, &raw ) ) )
		return false;
	view.reset( raw, texture.handle() );
	return true;
}

bool DMD3D::createShaderStage( SRVType type, const void* bytecode, size_t size, ShaderStage& stage )
{
	HRESULT hr = E_INVALIDARG;
	ID3D11DeviceChild* shader = nullptr;
	switch( type )
	{
		case SRVType::vs:
		{
			ID3D11VertexShader* raw = nullptr;
			hr = m_device->CreateVertexShader( bytecode, size, nullptr, &raw );
			shader = raw;
			break;
		}
		case SRVType::ps:
		{
			ID3D11PixelShader* raw = nullptr;
			hr = m_device->CreatePixelShader( bytecode, size, nullptr, &raw );
			shader = raw;
			break;
		}
		case SRVType::gs:
		{
			ID3D11GeometryShader* raw = nullptr;
			hr = m_device->CreateGeometryShader( bytecode, size, nullptr, &raw );
			shader = raw;
			break;
		}
		case SRVType::hs:
		{
			ID3D11HullShader* raw = nullptr;
			hr = m_device->CreateHullShader( bytecode, size, nullptr, &raw );
			shader = raw;
			break;
		}
		case SRVType::ds:
		{
			ID3D11DomainShader* raw = nullptr;
			hr = m_device->CreateDomainShader( bytecode, size, nullptr, &raw );
			shader = raw;
			break;
		}
		case SRVType::cs:
		{
			ID3D11ComputeShader* raw = nullptr;
			hr = m_device->CreateComputeShader( bytecode, size, nullptr, &raw );
			shader = raw;
			break;
		}
		default:
			break;
	}
	if( FAILED( hr ) )
		return false;
	stage.reset( type, shader );
	return true;
}

bool DMD3D::createInputLayout( const std::vector<VertexElement>& elements, const void* vsBytecode, size_t size, InputLayout& layout )
{
	std::vector<D3D11_INPUT_ELEMENT_DESC> descs( elements.size() );
	for( size_t i = 0; i < elements.size(); ++i )
	{
		const VertexElement& element = elements[i];
		descs[i].SemanticName = element.semantic;
		descs[i].SemanticIndex = element.semanticIndex;
		descs[i].Format = element.format;
		descs[i].InputSlot = element.slot;
		descs[i].AlignedByteOffset = element.offset == VertexElement::appendOffset ? D3D11_APPEND_ALIGNED_ELEMENT : element.offset;
		descs[i].InputSlotClass = element.perInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
		descs[i].InstanceDataStepRate = element.perInstance ? 1 : 0;
	}
	ID3D11InputLayout* raw = nullptr;
	if( FAILED( m_device->CreateInputLayout( descs.data(), static_cast<UINT>( descs.size() ), vsBytecode, size, &raw ) ) )
		return false;
	layout.reset( raw );
	return true;
}

void DMD3D::updateBuffer( Buffer& buffer, const void* data, size_t size )
{
	m_deviceContext->UpdateSubresource( buffer.handle(), 0, nullptr, data, static_cast<UINT>( size ), 0 );
}

void DMD3D::copyBuffer( Buffer& destination, const Buffer& source )
{
	m_deviceContext->CopyResource( destination.handle(), source.handle() );
}

bool DMD3D::readBuffer( const Buffer& readback, void* data, size_t size )
{
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if( FAILED( m_deviceContext->Map( readback.handle(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped ) ) )
		return false;
	memcpy( data, mapped.pData, size );
	m_deviceContext->Unmap( readback.handle(), 0 );
	return true;
}

void DMD3D::generateMips( const ShaderView& view )
{
	m_deviceContext->GenerateMips( view.handle() );
}

bool DMD3D::setConstantBuffer( SRVType type, uint16_t slot, const Buffer& buffer )
{
	if( buffer.ring() )
	{
		// Участок этого кадра; без записи в кадре привязывать нечего
		if( buffer.ringBytes() == 0 )
			return false;
		m_constantRing.bind( type, slot, buffer.ringOffset(), buffer.ringBytes() );
		return true;
	}

	ID3D11Buffer* cbuffer = buffer.handle();

	switch( type )
	{
		case SRVType::vs:
			m_deviceContext->VSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::ps:
			m_deviceContext->PSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::hs:
			m_deviceContext->HSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::ds:
			m_deviceContext->DSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::gs:
			m_deviceContext->GSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		case SRVType::cs:
			m_deviceContext->CSSetConstantBuffers( slot, 1, &cbuffer );
			break;
		default:
			return false;
	}

	return true;
}

void DMD3D::setConstantBufferAllStages( uint16_t slot, const Buffer& buffer )
{
	if( buffer.ring() )
	{
		for( SRVType stage : { SRVType::vs, SRVType::hs, SRVType::ds, SRVType::gs, SRVType::ps, SRVType::cs } )
			setConstantBuffer( stage, slot, buffer );
		return;
	}

	ID3D11Buffer* cbuffer = buffer.handle();
	m_deviceContext->VSSetConstantBuffers( slot, 1, &cbuffer );
	m_deviceContext->HSSetConstantBuffers( slot, 1, &cbuffer );
	m_deviceContext->DSSetConstantBuffers( slot, 1, &cbuffer );
	m_deviceContext->GSSetConstantBuffers( slot, 1, &cbuffer );
	m_deviceContext->PSSetConstantBuffers( slot, 1, &cbuffer );
	m_deviceContext->CSSetConstantBuffers( slot, 1, &cbuffer );
}

// Ресурс, который пишет текущий проход (цель или UAV compute), привязывается на вход: проход над ним закончен — цели
// снимаются, UAV отвязывается. Так ресурсы сцены (освещение окружением t101…t103, объём воздушной перспективы t106)
// привязываются сразу после прохода, который их посчитал; в D3D12 здесь — барьер в состояние чтения (D3D11 без этого
// снял бы вид сам, с предупреждением debug-слоя «still bound on output»)
void DMD3D::makeReadable( ID3D11Resource* resource )
{
	for( uint32_t i = 0; i < m_passTargetCount; ++i )
	{
		if( m_passTargets[i] == resource )
		{
			m_deviceContext->OMSetRenderTargets( 0, nullptr, nullptr );
			m_passTargetCount = 0;
			break;
		}
	}
	for( uint16_t slot = 0; slot < D3D11_1_UAV_SLOT_COUNT; ++slot )
	{
		if( m_boundUAVs[slot] == resource )
			unbindUAVs( slot, 1 );
	}
}

void DMD3D::setSRV( SRVType type, uint16_t slot, const ShaderView& view )
{
	ID3D11ShaderResourceView* const rawSRV = view.handle();
	if( view.resource() )
		makeReadable( view.resource() );
	if( slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT )
		m_boundSRVs[stageIndex( type )][slot] = view.resource();
	if( rawSRV && slot < SLOT_TRANSIENT_COUNT )
		m_boundTransientEnd[stageIndex( type )] = std::max<uint16_t>( m_boundTransientEnd[stageIndex( type )], slot + 1 );
	switch( type )
	{
		case SRVType::vs:
			m_deviceContext->VSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::ps:
			m_deviceContext->PSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::hs:
			m_deviceContext->HSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::ds:
			m_deviceContext->DSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::gs:
			m_deviceContext->GSSetShaderResources( slot, 1, &rawSRV );
			break;
		case SRVType::cs:
			m_deviceContext->CSSetShaderResources( slot, 1, &rawSRV );
			break;
		default:
			break;
	}
}

void DMD3D::unbindSRV( SRVType type, uint16_t slot, uint16_t count )
{
	ID3D11ShaderResourceView* views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
	count = std::min<uint16_t>( count, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT );
	for( uint16_t i = slot; i < std::min<uint32_t>( slot + count, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ); ++i )
		m_boundSRVs[stageIndex( type )][i] = nullptr;
	switch( type )
	{
		case SRVType::vs:
			m_deviceContext->VSSetShaderResources( slot, count, views );
			break;
		case SRVType::ps:
			m_deviceContext->PSSetShaderResources( slot, count, views );
			break;
		case SRVType::hs:
			m_deviceContext->HSSetShaderResources( slot, count, views );
			break;
		case SRVType::ds:
			m_deviceContext->DSSetShaderResources( slot, count, views );
			break;
		case SRVType::gs:
			m_deviceContext->GSSetShaderResources( slot, count, views );
			break;
		case SRVType::cs:
			m_deviceContext->CSSetShaderResources( slot, count, views );
			break;
		default:
			break;
	}
}

void DMD3D::setUAV( uint16_t slot, const StorageView& view )
{
	if( slot < D3D11_1_UAV_SLOT_COUNT )
		m_boundUAVs[slot] = view.resource();
	ID3D11UnorderedAccessView* views[1] = { view.handle() };
	const UINT counters[1] = { 0 };
	m_deviceContext->CSSetUnorderedAccessViews( slot, 1, views, counters );
}

void DMD3D::unbindUAVs( uint16_t first, uint16_t count )
{
	ID3D11UnorderedAccessView* views[D3D11_1_UAV_SLOT_COUNT] = {};
	UINT counters[D3D11_1_UAV_SLOT_COUNT] = {};
	count = std::min<uint16_t>( count, D3D11_1_UAV_SLOT_COUNT );
	m_deviceContext->CSSetUnorderedAccessViews( first, count, views, counters );
	for( uint16_t i = first; i < std::min<uint32_t>( first + count, D3D11_1_UAV_SLOT_COUNT ); ++i )
		m_boundUAVs[i] = nullptr;
}

void DMD3D::clearStorageView( const StorageView& view )
{
	const UINT zeros[4] = {};
	m_deviceContext->ClearUnorderedAccessViewUint( view.handle(), zeros );
}

void DMD3D::setShaderStage( SRVType type, const ShaderStage* stage )
{
	ID3D11DeviceChild* shader = stage ? stage->handle() : nullptr;
	switch( type )
	{
		case SRVType::vs:
			m_deviceContext->VSSetShader( static_cast<ID3D11VertexShader*>( shader ), nullptr, 0 );
			break;
		case SRVType::ps:
			m_deviceContext->PSSetShader( static_cast<ID3D11PixelShader*>( shader ), nullptr, 0 );
			break;
		case SRVType::gs:
			m_deviceContext->GSSetShader( static_cast<ID3D11GeometryShader*>( shader ), nullptr, 0 );
			break;
		case SRVType::hs:
			m_deviceContext->HSSetShader( static_cast<ID3D11HullShader*>( shader ), nullptr, 0 );
			break;
		case SRVType::ds:
			m_deviceContext->DSSetShader( static_cast<ID3D11DomainShader*>( shader ), nullptr, 0 );
			break;
		case SRVType::cs:
			m_deviceContext->CSSetShader( static_cast<ID3D11ComputeShader*>( shader ), nullptr, 0 );
			break;
		default:
			break;
	}
}

void DMD3D::unbindShaders()
{
	m_deviceContext->VSSetShader( nullptr, nullptr, 0 );
	m_deviceContext->GSSetShader( nullptr, nullptr, 0 );
	m_deviceContext->HSSetShader( nullptr, nullptr, 0 );
	m_deviceContext->DSSetShader( nullptr, nullptr, 0 );
	m_deviceContext->PSSetShader( nullptr, nullptr, 0 );
}

void DMD3D::setInputLayout( const InputLayout* layout )
{
	m_deviceContext->IASetInputLayout( layout ? layout->handle() : nullptr );
}

void DMD3D::setVertexBuffers( uint32_t count, const Buffer* const buffers[], const uint32_t strides[], const uint32_t offsets[] )
{
	ID3D11Buffer* raw[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
	count = std::min<uint32_t>( count, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT );
	for( uint32_t i = 0; i < count; ++i )
		raw[i] = buffers[i]->handle();
	m_deviceContext->IASetVertexBuffers( 0, count, raw, strides, offsets );
}

void DMD3D::setVertexBuffer( const Buffer& buffer, uint32_t stride, uint32_t offset )
{
	ID3D11Buffer* raw = buffer.handle();
	m_deviceContext->IASetVertexBuffers( 0, 1, &raw, &stride, &offset );
}

void DMD3D::setIndexBuffer( const Buffer& buffer, DXGI_FORMAT format, uint32_t offset )
{
	m_deviceContext->IASetIndexBuffer( buffer.handle(), format, offset );
}

void DMD3D::unbindGeometry()
{
	ID3D11Buffer* none = nullptr;
	const UINT zero = 0;
	m_deviceContext->IASetVertexBuffers( 0, 1, &none, &zero, &zero );
	m_deviceContext->IASetIndexBuffer( nullptr, DXGI_FORMAT_R32_UINT, 0 );
}

void DMD3D::setTopology( D3D_PRIMITIVE_TOPOLOGY topology )
{
	m_deviceContext->IASetPrimitiveTopology( topology );
}

void DMD3D::draw( uint32_t vertexCount, uint32_t startVertex )
{
	m_deviceContext->Draw( vertexCount, startVertex );
}

void DMD3D::drawIndexed( uint32_t indexCount, uint32_t startIndex, int32_t baseVertex )
{
	m_deviceContext->DrawIndexed( indexCount, startIndex, baseVertex );
}

void DMD3D::drawIndexedInstanced( uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance )
{
	m_deviceContext->DrawIndexedInstanced( indexCount, instanceCount, startIndex, baseVertex, startInstance );
}

void DMD3D::drawIndexedInstancedIndirect( const Buffer& args, uint32_t argsOffset )
{
	m_deviceContext->DrawIndexedInstancedIndirect( args.handle(), argsOffset );
}

void DMD3D::drawAuto()
{
	m_deviceContext->DrawAuto();
}

void DMD3D::dispatch( uint32_t x, uint32_t y, uint32_t z )
{
	m_deviceContext->Dispatch( x, y, z );
}

#include "ScreenGrab.h"
#include <wincodecsdk.h>

bool DMD3D::saveScreenshot( const std::wstring& path )
{
	ID3D11Texture2D* backBuffer = nullptr;
	HRESULT hr = m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );
	if( FAILED( hr ) )
		return false;
	const bool jpeg = path.size() > 4 && ( _wcsicmp( path.c_str() + path.size() - 4, L".jpg" ) == 0 ||
										   _wcsicmp( path.c_str() + path.size() - 5, L".jpeg" ) == 0 );
	hr = SaveWICTextureToFile( m_deviceContext.get(), backBuffer, jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng,
							   path.c_str() );
	backBuffer->Release();
	return SUCCEEDED( hr );
}

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