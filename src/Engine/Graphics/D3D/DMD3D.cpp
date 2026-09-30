#include "DMD3D.h"
#include "Shaders\slots.h"
#include "Utils\utilites.h"
#include "Logger\Logger.h"
#include "DMSamplerState.h"
#include <D3D12MemAlloc.h>
#include <d3dx12.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Agility SDK: рантайм D3D12 из пакета рядом с exe (D3D12\D3D12Core.dll), а не системный — версия из CMakeLists.txt.
// Загрузчик d3d12.dll читает эти экспорты при первом D3D12CreateDevice
extern "C"
{
	__declspec( dllexport ) extern const UINT D3D12SDKVersion = DM_AGILITY_SDK_VERSION;
	__declspec( dllexport ) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

std::unique_ptr<DMD3D> DMD3D::m_instance;

namespace
{

bool isDepthFormat( DXGI_FORMAT format )
{
	return format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT || format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
		   format == DXGI_FORMAT_D16_UNORM;
}

// Формат глубины для typeless-текстуры (карта теней — R32_TYPELESS: DSV D32_FLOAT, SRV R32_FLOAT)
DXGI_FORMAT depthFormatFor( DXGI_FORMAT format )
{
	switch( format )
	{
		case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_D16_UNORM;
		default: return format;
	}
}

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

uint32_t subresourceCount( const TextureDesc& desc )
{
	return desc.mipCount * ( desc.depth > 1 ? 1 : desc.arraySize );
}

}

// ---------------------------------------------------------------------------------------------------------------------
// Освобождение дескрипторов видов и ресурсов (GpuResources.h): отложенное, пока GPU не закончит кадр
// ---------------------------------------------------------------------------------------------------------------------

void gpuFreeShaderDescriptor( const Descriptor& descriptor )
{
	if( DMD3D::exists() )
		DMD3D::instance().deferFreeDescriptor( DMD3D::HeapKind::shader, descriptor );
}

void gpuFreeStagingDescriptor( const Descriptor& descriptor )
{
	if( DMD3D::exists() )
		DMD3D::instance().deferFreeDescriptor( DMD3D::HeapKind::staging, descriptor );
}

void gpuFreeTargetDescriptor( const Descriptor& descriptor, bool depth )
{
	if( DMD3D::exists() )
		DMD3D::instance().deferFreeDescriptor( depth ? DMD3D::HeapKind::dsv : DMD3D::HeapKind::rtv, descriptor );
}

void gpuReleaseResource( ID3D12Resource* resource, D3D12MA::Allocation* allocation )
{
	if( !resource && !allocation )
		return;
	if( DMD3D::exists() && DMD3D::instance().m_device )
	{
		DMD3D& d3d = DMD3D::instance();
		d3d.m_states.erase( resource );
		d3d.deferRelease( resource );
		d3d.deferRelease( allocation );
		return;
	}
	// DMD3D уже нет (ресурсы хранилищ живут до конца процесса): отпускаем сразу — устройство держат сами объекты
	if( resource )
		resource->Release();
	if( allocation )
		allocation->Release();
}

// ---------------------------------------------------------------------------------------------------------------------

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

	if( !selectAdapter() || !createDevice( config ) || !createQueuesAndFrames() || !createDescriptorHeaps() ||
		!createSwapChain( hwnd, config.fullScreen() ) || !createBackBufferTargets() || !createRootSignature() )
		return false;
	if( !m_constantRing.initialize( m_device.get(), constantRingBytes, frameCount ) )
		return false;
	loadPipelineLibrary();

	// Команды загрузки (копии данных, мипы куба неба) записываются в список первого кадра; beginFrame их выполнит и дождётся
	openCommandList( m_frames[0] );
	return true;
}

bool DMD3D::selectAdapter()
{
	// Видеокарта: на машине с двумя GPU адаптер 0 — обычно встроенная, движку нужна дискретная. Выбор — как
	// «Высокая производительность» в настройках графики Windows: IDXGIFactory6::EnumAdapterByGpuPreference;
	// программные адаптеры (Microsoft Basic Render Driver) пропускаются
	UINT factoryFlags = 0;
#ifdef _DEBUG
	factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
	IDXGIFactory4* factoryPtr = nullptr;
	if( FAILED( CreateDXGIFactory2( factoryFlags, __uuidof( IDXGIFactory4 ), (void**)&factoryPtr ) ) )
	{
		LOG( "CreateDXGIFactory2 failed" );
		return false;
	}
	m_factory = make_com_ptr<IDXGIFactory4>( factoryPtr );

	IDXGIFactory6* factory6Ptr = nullptr;
	if( SUCCEEDED( m_factory->QueryInterface( __uuidof( IDXGIFactory6 ), (void**)&factory6Ptr ) ) )
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
		if( FAILED( m_factory->EnumAdapters1( 0, &adapterPtr ) ) )
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
		if( SUCCEEDED( output->GetDisplayModeList( backBufferFormat, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr ) ) && numModes )
		{
			std::vector<DXGI_MODE_DESC> modes( numModes );
			if( SUCCEEDED( output->GetDisplayModeList( backBufferFormat, DXGI_ENUM_MODES_INTERLACED, &numModes, modes.data() ) ) )
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
	return true;
}

bool DMD3D::createDevice( const Config& config )
{
#ifdef _DEBUG
	// Debug-слой и GPU-based validation (состояния ресурсов, индексы дескрипторов, барьеры — то, что D3D11 прощал)
	// только в Debug-сборке: они заметно замедляют кадр. GpuValidation=false в settings.ini оставляет только слой.
	// Слой — из redist Agility SDK (D3D12\d3d12SDKLayers.dll), компонент Windows «Средства графики» не нужен
	ID3D12Debug* debugPtr = nullptr;
	if( SUCCEEDED( D3D12GetDebugInterface( __uuidof( ID3D12Debug ), (void**)&debugPtr ) ) )
	{
		auto debug = make_com_ptr<ID3D12Debug>( debugPtr );
		debug->EnableDebugLayer();
		ID3D12Debug1* debug1Ptr = nullptr;
		if( SUCCEEDED( debug->QueryInterface( __uuidof( ID3D12Debug1 ), (void**)&debug1Ptr ) ) )
		{
			auto debug1 = make_com_ptr<ID3D12Debug1>( debug1Ptr );
			debug1->SetEnableGPUBasedValidation( config.gpuValidation() );
		}
		LOG( std::string( "D3D12 debug layer is enabled" ) + ( config.gpuValidation() ? " with GPU-based validation" : "" ) +
			 ", its messages are written to this log" );
	}
	else
		LOG( "D3D12 debug layer is unavailable (D3D12\\d3d12SDKLayers.dll), creating device without it" );

	// DRED: при потере устройства — какая команда выполнялась последней и где случился page fault (logDeviceRemoved)
	ID3D12DeviceRemovedExtendedDataSettings1* dredPtr = nullptr;
	if( SUCCEEDED( D3D12GetDebugInterface( __uuidof( ID3D12DeviceRemovedExtendedDataSettings1 ), (void**)&dredPtr ) ) )
	{
		auto dred = make_com_ptr<ID3D12DeviceRemovedExtendedDataSettings1>( dredPtr );
		dred->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
		dred->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
		dred->SetBreadcrumbContextEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
	}
#else
	(void)config;
#endif

	ID3D12Device10* devicePtr = nullptr;
	if( FAILED( D3D12CreateDevice( m_adapter.get(), D3D_FEATURE_LEVEL_12_0, __uuidof( ID3D12Device10 ), (void**)&devicePtr ) ) )
	{
		LOG( "D3D12 device (feature level 12_0, ID3D12Device10 — Agility SDK) is not available" );
		return false;
	}
	m_device = make_com_ptr<ID3D12Device10>( devicePtr );
	m_device->SetName( L"DMEngine device" );

	// Без запасных путей: bindless (Tier 3 и ResourceDescriptorHeap SM 6.6) и enhanced barriers — основа слоя
	D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
	D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
	D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
	m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) );
	m_device->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof( shaderModel ) );
	m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof( options12 ) );
	m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof( options16 ) );
	if( options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3 )
	{
		LOG( "Resource Binding Tier 3 is required (bindless)" );
		return false;
	}
	if( shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6 )
	{
		LOG( "Shader Model 6.6 is required (ResourceDescriptorHeap)" );
		return false;
	}
	if( !options12.EnhancedBarriersSupported )
	{
		LOG( "Enhanced barriers are required: check that D3D12\\D3D12Core.dll (Agility SDK) is next to the executable and the driver is recent" );
		return false;
	}
	LOG( "D3D12 device: Agility SDK " + std::to_string( D3D12SDKVersion ) + ", feature level 12_0, shader model " +
		 std::to_string( shaderModel.HighestShaderModel >> 4 ) + "." + std::to_string( shaderModel.HighestShaderModel & 0xF ) +
		 ", resource binding tier " + std::to_string( static_cast<int>( options.ResourceBindingTier ) ) + ", enhanced barriers" +
		 ( options16.GPUUploadHeapSupported ? ", GPU upload heaps" : ", no GPU upload heaps" ) );

#ifdef _DEBUG
	ID3D12InfoQueue1* infoQueuePtr = nullptr;
	if( SUCCEEDED( m_device->QueryInterface( __uuidof( ID3D12InfoQueue1 ), (void**)&infoQueuePtr ) ) )
	{
		m_infoQueue = make_com_ptr<ID3D12InfoQueue1>( infoQueuePtr );
		m_infoQueue->RegisterMessageCallback( messageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &m_messageCookie );
	}
#endif

	// Память ресурсов — D3D12MA: блоки куч под ресурсы, без своего аллокатора
	D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
	allocatorDesc.Flags = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;
	allocatorDesc.pDevice = m_device.get();
	allocatorDesc.pAdapter = m_adapter.get();
	if( FAILED( D3D12MA::CreateAllocator( &allocatorDesc, &m_allocator ) ) )
	{
		LOG( "D3D12 Memory Allocator: CreateAllocator failed" );
		return false;
	}
	return true;
}

void DMD3D::messageCallback( D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context )
{
	if( severity == D3D12_MESSAGE_SEVERITY_INFO || severity == D3D12_MESSAGE_SEVERITY_MESSAGE )
		return;
	// Пайплайна нет в библиотеке на диске — штатно: он собирается и кладётся туда (createPipelineObject)
	if( id == D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND )
		return;

	// Одно и то же сообщение обычно повторяется каждый кадр: пишется только первые maxRepeats раз
	const uint32_t maxRepeats = 3;
	DMD3D* self = static_cast<DMD3D*>( context );
	const uint32_t repeats = ++self->m_debugMessageCounts[static_cast<int>( id )];
	if( repeats > maxRepeats )
		return;
	const char* severityName = severity == D3D12_MESSAGE_SEVERITY_WARNING ? "warning" :
							   severity == D3D12_MESSAGE_SEVERITY_ERROR ? "error" : "corruption";
	LOG( std::string( "D3D12 " ) + severityName + ": " + ( description ? description : "" ) +
		 ( repeats == maxRepeats ? " (further repeats are not logged)" : "" ) );
}

bool DMD3D::createQueuesAndFrames()
{
	const auto createQueue = [this]( D3D12_COMMAND_LIST_TYPE type, const wchar_t* name, com_unique_ptr<ID3D12CommandQueue>& queue )
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = type;
		ID3D12CommandQueue* raw = nullptr;
		if( FAILED( m_device->CreateCommandQueue( &desc, __uuidof( ID3D12CommandQueue ), (void**)&raw ) ) )
			return false;
		raw->SetName( name );
		queue = make_com_ptr<ID3D12CommandQueue>( raw );
		return true;
	};
	if( !createQueue( D3D12_COMMAND_LIST_TYPE_DIRECT, L"Direct queue", m_directQueue ) ||
		!createQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE, L"Compute queue", m_computeQueue ) ||
		!createQueue( D3D12_COMMAND_LIST_TYPE_COPY, L"Copy queue", m_copyQueue ) )
	{
		LOG( "Failed to create command queues" );
		return false;
	}

	ID3D12Fence* fence = nullptr;
	if( FAILED( m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, __uuidof( ID3D12Fence ), (void**)&fence ) ) )
		return false;
	m_fence = make_com_ptr<ID3D12Fence>( fence );
	m_fenceValue = 0;
	m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
	if( !m_fenceEvent )
		return false;

	for( uint32_t i = 0; i < frameCount; ++i )
	{
		ID3D12CommandAllocator* allocator = nullptr;
		if( FAILED( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof( ID3D12CommandAllocator ), (void**)&allocator ) ) )
			return false;
		allocator->SetName( ( L"Frame allocator " + std::to_wstring( i ) ).c_str() );
		m_frames[i].allocator = make_com_ptr<ID3D12CommandAllocator>( allocator );
		m_frames[i].fenceValue = 0;
	}

	// Один графический список на кадр (запись в один поток); создаётся закрытым, открывает его openCommandList
	ID3D12GraphicsCommandList7* list = nullptr;
	if( FAILED( m_device->CreateCommandList1( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
											   __uuidof( ID3D12GraphicsCommandList7 ), (void**)&list ) ) )
	{
		LOG( "ID3D12GraphicsCommandList7 (enhanced barriers) is not available" );
		return false;
	}
	list->SetName( L"Frame command list" );
	m_commandList = make_com_ptr<ID3D12GraphicsCommandList7>( list );
	m_recording = false;
	return true;
}

bool DMD3D::createDescriptorHeaps()
{
	// Одна shader-visible куча на всё (Tier 3 — до миллиона): дескриптор выдаётся виду навсегда, его номер — индекс
	// ResourceDescriptorHeap в шейдере. CPU-копии UAV нужны ClearUnorderedAccessView
	if( !m_shaderHeap.create( m_device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, true, L"Shader-visible descriptors" ) ||
		!m_stagingHeap.create( m_device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, false, L"UAV clear descriptors" ) ||
		!m_rtvHeap.create( m_device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256, false, L"RTV descriptors" ) ||
		!m_dsvHeap.create( m_device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64, false, L"DSV descriptors" ) )
	{
		LOG( "Failed to create descriptor heaps" );
		return false;
	}

	// Дескриптор 0 — пустой SRV: непривязанный слот таблицы привязок читает нули, а не чужой ресурс
	m_nullDescriptor = m_shaderHeap.allocate();
	D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
	nullDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullDesc.Texture2D.MipLevels = 1;
	m_device->CreateShaderResourceView( nullptr, &nullDesc, m_nullDescriptor.cpu );
	return true;
}

Descriptor DMD3D::allocateShaderDescriptor()
{
	const Descriptor descriptor = m_shaderHeap.allocate();
	if( !descriptor.valid() )
		LOG( "Shader-visible descriptor heap is full (" + std::to_string( m_shaderHeap.capacity() ) + ")" );
	return descriptor;
}

void DMD3D::freeShaderDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE cpu )
{
	deferFreeDescriptor( HeapKind::shader, m_shaderHeap.at( m_shaderHeap.indexOf( cpu ) ) );
}

void DMD3D::deferFreeDescriptor( HeapKind heap, const Descriptor& descriptor )
{
	if( !descriptor.valid() )
		return;
	if( m_device )
		m_frames[m_frameIndex].deferredDescriptors.emplace_back( heap, descriptor );
}

void DMD3D::deferRelease( IUnknown* object )
{
	if( !object )
		return;
	if( m_device )
		m_frames[m_frameIndex].deferredReleases.push_back( object );
	else
		object->Release();
}

void DMD3D::processDeferred( FrameResources& frame )
{
	for( IUnknown* object : frame.deferredReleases )
		object->Release();
	frame.deferredReleases.clear();
	for( const auto& [heap, descriptor] : frame.deferredDescriptors )
	{
		switch( heap )
		{
			case HeapKind::shader: m_shaderHeap.free( descriptor ); break;
			case HeapKind::staging: m_stagingHeap.free( descriptor ); break;
			case HeapKind::rtv: m_rtvHeap.free( descriptor ); break;
			case HeapKind::dsv: m_dsvHeap.free( descriptor ); break;
		}
	}
	frame.deferredDescriptors.clear();
}

bool DMD3D::createSwapChain( HWND hwnd, bool fullscreen )
{
	// Swap chain — flip model (единственная в D3D12): кадр отдаётся композитору без копирования, задних буферов два.
	// Без vsync — DXGI_PRESENT_ALLOW_TEARING, иначе flip model упёрлась бы в частоту монитора. Waitable object —
	// кадр начинается, когда очередь кадров короче предела (waitForNextFrame). Оба флага — только в окне
	const bool windowed = !fullscreen;
	m_allowTearing = false;
	IDXGIFactory5* factory5Ptr = nullptr;
	if( windowed && SUCCEEDED( m_factory->QueryInterface( __uuidof( IDXGIFactory5 ), (void**)&factory5Ptr ) ) )
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
	desc.SampleDesc.Count = 1;
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

	IDXGISwapChain1* swapChain1 = nullptr;
	if( FAILED( m_factory->CreateSwapChainForHwnd( m_directQueue.get(), hwnd, &desc, &fullscreenDesc, nullptr, &swapChain1 ) ) )
	{
		LOG( "CreateSwapChainForHwnd failed" );
		return false;
	}
	IDXGISwapChain3* swapChain3 = nullptr;
	const HRESULT hr = swapChain1->QueryInterface( __uuidof( IDXGISwapChain3 ), (void**)&swapChain3 );
	swapChain1->Release();
	if( FAILED( hr ) )
	{
		LOG( "IDXGISwapChain3 is required" );
		return false;
	}
	m_swapChain = make_com_ptr<IDXGISwapChain3>( swapChain3 );
	// Alt+Enter не переключает полноэкранный режим сам: размер целей меняет только WM_SIZE
	m_factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER );

	if( m_swapChainFlags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT )
	{
		// Два кадра в очереди: CPU готовит следующий, пока GPU рисует текущий
		m_swapChain->SetMaximumFrameLatency( frameCount );
		m_frameLatencyWaitable = m_swapChain->GetFrameLatencyWaitableObject();
	}
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	LOG( std::string( "Swap chain: flip model, " ) + std::to_string( backBufferCount ) + " buffers" +
		 ( m_allowTearing ? ", tearing allowed" : ", no tearing" ) + ( m_frameLatencyWaitable ? ", frame latency waitable object" : "" ) +
		 ", " + std::to_string( frameCount ) + " frames in flight, constant ring " + std::to_string( constantRingBytes / 1024 / frameCount ) + " KB per frame" );
	return true;
}

bool DMD3D::createBackBufferTargets()
{
	for( uint32_t i = 0; i < backBufferCount; ++i )
	{
		ID3D12Resource* backBuffer = nullptr;
		if( FAILED( m_swapChain->GetBuffer( i, __uuidof( ID3D12Resource ), (void**)&backBuffer ) ) )
			return false;
		backBuffer->SetName( ( L"Back buffer " + std::to_wstring( i ) ).c_str() );
		m_backBuffers[i] = make_com_ptr<ID3D12Resource>( backBuffer );

		// Задний буфер flip model — UNORM; вид — sRGB, байты в буфере те же, что были бы у R8G8B8A8_UNORM_SRGB
		const Descriptor descriptor = m_rtvHeap.allocate();
		if( !descriptor.valid() )
			return false;
		D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
		viewDesc.Format = backBufferViewFormat;
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		m_device->CreateRenderTargetView( backBuffer, &viewDesc, descriptor.cpu );
		m_backBufferTargets[i].reset( descriptor, false, backBufferViewFormat, backBuffer, { 0, 1, 0, 1 } );
	}
	return true;
}

void DMD3D::releaseBackBufferTargets()
{
	for( uint32_t i = 0; i < backBufferCount; ++i )
	{
		m_backBufferTargets[i].reset();
		m_backBuffers[i].reset();
	}
}

bool DMD3D::createSceneTargets( const float clearColor[4] )
{
	memcpy( m_sceneClearColor, clearColor, sizeof( m_sceneClearColor ) );
	TextureDesc colorDesc;
	colorDesc.width = m_screenWidth;
	colorDesc.height = m_screenHeight;
	colorDesc.format = sceneColorFormat;
	colorDesc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource;
	TextureDesc depthDesc;
	depthDesc.width = m_screenWidth;
	depthDesc.height = m_screenHeight;
	depthDesc.format = sceneDepthFormat;
	depthDesc.usage = TextureUsage::depthStencil;
	if( !createTextureInternal( colorDesc, nullptr, m_sceneTexture, clearColor, L"Scene color" ) ||
		!createTargetView( m_sceneTexture, {}, m_sceneTarget ) || !createShaderView( m_sceneTexture, {}, m_sceneSRV ) ||
		!createTextureInternal( depthDesc, nullptr, m_sceneDepthTexture, nullptr, L"Scene depth" ) ||
		!createTargetView( m_sceneDepthTexture, {}, m_sceneDepth ) )
	{
		LOG( "Failed to create the scene color and depth buffers" );
		return false;
	}
	return true;
}

void DMD3D::releaseSceneTargets()
{
	m_sceneSRV.reset();
	m_sceneTarget.reset();
	m_sceneDepth.reset();
	m_sceneTexture.reset();
	m_sceneDepthTexture.reset();
}

DMD3D::VideoMemory DMD3D::videoMemory() const
{
	VideoMemory memory;
	IDXGIAdapter3* adapter3 = nullptr;
	if( m_adapter && SUCCEEDED( m_adapter->QueryInterface( __uuidof( IDXGIAdapter3 ), (void**)&adapter3 ) ) )
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
		if( SUCCEEDED( adapter3->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info ) ) )
		{
			memory.usedBytes = info.CurrentUsage;
			memory.budgetBytes = info.Budget;
		}
		adapter3->Release();
	}
	return memory;
}

// ---------------------------------------------------------------------------------------------------------------------
// Кадр
// ---------------------------------------------------------------------------------------------------------------------

void DMD3D::openCommandList( FrameResources& frame )
{
	if( m_recording )
		return;
	m_commandList->Reset( frame.allocator.get(), nullptr );
	ID3D12DescriptorHeap* heaps[] = { m_shaderHeap.handle() };
	m_commandList->SetDescriptorHeaps( 1, heaps );
	m_recording = true;
}

void DMD3D::submitCommandList()
{
	if( !m_recording )
		return;
	flushBarriers();
	m_recording = false;
	if( FAILED( m_commandList->Close() ) )
	{
		LOG( "Command list Close failed: a recorded command is invalid (see debug layer messages above)" );
		return;
	}
	ID3D12CommandList* lists[] = { m_commandList.get() };
	m_directQueue->ExecuteCommandLists( 1, lists );
}

uint64_t DMD3D::signalFence()
{
	++m_fenceValue;
	m_directQueue->Signal( m_fence.get(), m_fenceValue );
	return m_fenceValue;
}

void DMD3D::waitForFence( uint64_t value )
{
	if( value == 0 || m_fence->GetCompletedValue() >= value )
		return;
	if( SUCCEEDED( m_fence->SetEventOnCompletion( value, m_fenceEvent ) ) )
		WaitForSingleObject( m_fenceEvent, INFINITE );
}

void DMD3D::waitForNextFrame()
{
	if( m_frameLatencyWaitable )
		WaitForSingleObjectEx( m_frameLatencyWaitable, 1000, TRUE );
}

void DMD3D::waitForGpu()
{
	// Всё записанное — в очередь, затем ждём её конца; открытый список открывается заново на том же аллокаторе
	const bool wasRecording = m_recording;
	submitCommandList();
	waitForFence( signalFence() );
	for( FrameResources& frame : m_frames )
	{
		processDeferred( frame );
		frame.fenceValue = 0;
	}
	if( wasRecording )
	{
		m_frames[m_frameIndex].allocator->Reset();
		openCommandList( m_frames[m_frameIndex] );
	}
}

void DMD3D::beginFrame()
{
	waitForNextFrame();

	if( m_recording )
	{
		// Команды загрузки: выполнить и дождаться до первого кадра — иначе кадр начал бы переписывать их данные
		submitCommandList();
		waitForFence( signalFence() );
	}
	m_frameStarted = true;

	FrameResources& frame = m_frames[m_frameIndex];
	waitForFence( frame.fenceValue );
	processDeferred( frame );
	frame.allocator->Reset();
	openCommandList( frame );

	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
	m_constantRing.beginFrame( m_frameIndex );
	m_passFormats = {};
	if( m_passLogRequested )
	{
		m_passLogRequested = false;
		m_recordingPasses = true;
		m_passRecords.clear();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Барьеры (enhanced barriers)
// ---------------------------------------------------------------------------------------------------------------------

D3D12_BARRIER_SYNC DMD3D::syncFor( D3D12_BARRIER_ACCESS access )
{
	switch( access )
	{
		case D3D12_BARRIER_ACCESS_COPY_SOURCE:
		case D3D12_BARRIER_ACCESS_COPY_DEST: return D3D12_BARRIER_SYNC_COPY;
		case D3D12_BARRIER_ACCESS_RENDER_TARGET: return D3D12_BARRIER_SYNC_RENDER_TARGET;
		case D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE:
		case D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ: return D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		case D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT: return D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		case D3D12_BARRIER_ACCESS_INDEX_BUFFER: return D3D12_BARRIER_SYNC_INDEX_INPUT;
		case D3D12_BARRIER_ACCESS_VERTEX_BUFFER: return D3D12_BARRIER_SYNC_VERTEX_SHADING;
		case D3D12_BARRIER_ACCESS_NO_ACCESS: return D3D12_BARRIER_SYNC_NONE;
		default: return D3D12_BARRIER_SYNC_ALL_SHADING;	// SRV, UAV, константы
	}
}

D3D12_BARRIER_ACCESS DMD3D::steadyAccess( uint32_t usage )
{
	// Чем буфер читается после записи с CPU: аргументы — ExecuteIndirect, вершины и индексы — IA, остальное — шейдеры
	if( usage & BufferUsage::indirectArgs )
		return D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
	if( usage & BufferUsage::vertex )
		return D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
	if( usage & BufferUsage::index )
		return D3D12_BARRIER_ACCESS_INDEX_BUFFER;
	if( usage & BufferUsage::constant )
		return D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
	if( usage & BufferUsage::shaderResource )
		return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
	if( usage & BufferUsage::unorderedAccess )
		return D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
	return D3D12_BARRIER_ACCESS_COPY_SOURCE;
}

void DMD3D::barrier( ID3D12Resource* resource, D3D12_BARRIER_ACCESS access, D3D12_BARRIER_LAYOUT layout, bool force )
{
	if( !resource )
		return;
	auto found = m_states.find( resource );
	if( found == m_states.end() )
		return;	// задний буфер и ресурсы без учёта состояния — свои барьеры
	ResourceState& state = found->second;
	if( !force && state.access == access && ( !state.texture || state.layout == layout ) )
		return;

	const D3D12_BARRIER_SYNC syncAfter = syncFor( access );
	if( state.texture )
	{
		D3D12_TEXTURE_BARRIER textureBarrier = {};
		textureBarrier.SyncBefore = state.sync;
		textureBarrier.SyncAfter = syncAfter;
		textureBarrier.AccessBefore = state.access;
		textureBarrier.AccessAfter = access;
		textureBarrier.LayoutBefore = state.layout;
		textureBarrier.LayoutAfter = layout;
		textureBarrier.pResource = resource;
		textureBarrier.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFFu;
		m_pendingTextureBarriers.push_back( textureBarrier );
		state.layout = layout;
	}
	else
	{
		D3D12_BUFFER_BARRIER bufferBarrier = {};
		bufferBarrier.SyncBefore = state.sync;
		bufferBarrier.SyncAfter = syncAfter;
		bufferBarrier.AccessBefore = state.access;
		bufferBarrier.AccessAfter = access;
		bufferBarrier.pResource = resource;
		bufferBarrier.Size = UINT64_MAX;
		m_pendingBufferBarriers.push_back( bufferBarrier );
	}
	state.sync = syncAfter;
	state.access = access;
}

void DMD3D::flushBarriers()
{
	if( m_pendingBufferBarriers.empty() && m_pendingTextureBarriers.empty() )
		return;
	D3D12_BARRIER_GROUP groups[2];
	uint32_t count = 0;
	if( !m_pendingBufferBarriers.empty() )
	{
		groups[count].Type = D3D12_BARRIER_TYPE_BUFFER;
		groups[count].NumBarriers = static_cast<UINT32>( m_pendingBufferBarriers.size() );
		groups[count].pBufferBarriers = m_pendingBufferBarriers.data();
		++count;
	}
	if( !m_pendingTextureBarriers.empty() )
	{
		groups[count].Type = D3D12_BARRIER_TYPE_TEXTURE;
		groups[count].NumBarriers = static_cast<UINT32>( m_pendingTextureBarriers.size() );
		groups[count].pTextureBarriers = m_pendingTextureBarriers.data();
		++count;
	}
	m_commandList->Barrier( count, groups );
	m_pendingBufferBarriers.clear();
	m_pendingTextureBarriers.clear();
}

void DMD3D::BeginScene( float red, float green, float blue, float alpha )
{
	const float color[4] = { red, green, blue, alpha };

	// Задний буфер — цель этого кадра (у него свой учёт: PRESENT ↔ RENDER_TARGET)
	D3D12_TEXTURE_BARRIER present = {};
	present.SyncBefore = D3D12_BARRIER_SYNC_NONE;
	present.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
	present.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
	present.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
	present.LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT;
	present.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	present.pResource = m_backBuffers[m_backBufferIndex].get();
	present.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFFu;
	m_pendingTextureBarriers.push_back( present );

	// Буфер сцены создаётся с этим цветом как optimized clear value — иначе debug-слой предупреждает при каждой очистке
	if( !m_sceneTarget.valid() && !createSceneTargets( color ) )
		return;

	// Сцена рисуется в HDR-буфер; цвет очистки — линейный, как всё в нём. Обратная глубина: очищенный буфер — дальняя
	// плоскость, 0. Очистка требует layout цели (веха M4 перенесёт барьеры целей в beginPass)
	barrier( m_sceneTexture.handle(), D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET );
	barrier( m_sceneDepthTexture.handle(), D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE );
	beginPass( PassDesc{ "Scene clear", { { &m_sceneTarget, "scene color" } }, { &m_sceneDepth, "scene depth" }, m_screenWidth, m_screenHeight } );
	clearTarget( m_sceneTarget, color );
	clearDepth( m_sceneDepth, 0.0f );
	// До вехи M4 тонмаппинг не рисует: цвет очистки в задний буфер, чтобы окно было не чёрным
	clearTarget( backBufferTarget(), color );
}

void DMD3D::beginPass( const PassDesc& pass )
{
	// Цели и область вывода; барьеры целей, чтения и записи по объявлению — веха M4
	D3D12_CPU_DESCRIPTOR_HANDLE targets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	m_passFormats = {};
	for( const PassDesc::Target& target : pass.colors )
	{
		if( target.view && target.view->valid() && m_passFormats.colorCount < TargetFormats::maxColors )
		{
			targets[m_passFormats.colorCount] = target.view->handle();
			m_passFormats.color[m_passFormats.colorCount++] = target.view->format();
		}
	}
	const bool hasDepth = pass.depth.view && pass.depth.view->valid();
	const D3D12_CPU_DESCRIPTOR_HANDLE depth = hasDepth ? pass.depth.view->handle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
	m_passFormats.depth = hasDepth ? pass.depth.view->format() : DXGI_FORMAT_UNKNOWN;
	flushBarriers();
	m_commandList->OMSetRenderTargets( m_passFormats.colorCount, m_passFormats.colorCount ? targets : nullptr, FALSE, hasDepth ? &depth : nullptr );

	if( pass.width && pass.height )
	{
		D3D12_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>( pass.width );
		viewport.Height = static_cast<float>( pass.height );
		viewport.MaxDepth = 1.0f;
		const D3D12_RECT scissor = { 0, 0, static_cast<LONG>( pass.width ), static_cast<LONG>( pass.height ) };
		m_commandList->RSSetViewports( 1, &viewport );
		m_commandList->RSSetScissorRects( 1, &scissor );
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

void DMD3D::clearDepth( const TargetView& target, float depth )
{
	if( !target.valid() || !target.isDepth() )
		return;
	barrier( target.resource(), D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE );
	flushBarriers();
	m_commandList->ClearDepthStencilView( target.handle(), D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr );
}

void DMD3D::clearTarget( const TargetView& target, const float color[4] )
{
	if( !target.valid() || target.isDepth() )
		return;
	barrier( target.resource(), D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET );
	flushBarriers();
	m_commandList->ClearRenderTargetView( target.handle(), color, 0, nullptr );
}

void DMD3D::EndScene()
{
	// Задний буфер — на показ
	D3D12_TEXTURE_BARRIER present = {};
	present.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
	present.SyncAfter = D3D12_BARRIER_SYNC_NONE;
	present.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
	present.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
	present.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
	present.LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT;
	present.pResource = m_backBuffers[m_backBufferIndex].get();
	present.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFFu;
	m_pendingTextureBarriers.push_back( present );
	submitCommandList();

	// С vsync — по частоте монитора; без него — сразу, с разрывом кадра (tearing), если DXGI его поддерживает
	const UINT flags = !m_vsync_enabled && m_allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
	const HRESULT hr = m_swapChain->Present( m_vsync_enabled ? 1 : 0, flags );
	m_frames[m_frameIndex].fenceValue = signalFence();
	if( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET )
		logDeviceRemoved( m_device->GetDeviceRemovedReason() );

	if( m_recordingPasses )
	{
		m_recordingPasses = false;
		LOG( "Frame passes: " + std::to_string( m_passRecords.size() ) );
		for( size_t i = 0; i < m_passRecords.size(); ++i )
			LOG( "  " + std::to_string( i ) + ". " + m_passRecords[i] );
	}

	m_frameIndex = ( m_frameIndex + 1 ) % frameCount;
}

void DMD3D::logDeviceRemoved( HRESULT reason )
{
	LOG( "Device removed, reason " + std::to_string( static_cast<long>( reason ) ) );
	ID3D12DeviceRemovedExtendedData1* dredPtr = nullptr;
	if( FAILED( m_device->QueryInterface( __uuidof( ID3D12DeviceRemovedExtendedData1 ), (void**)&dredPtr ) ) )
		return;
	auto dred = make_com_ptr<ID3D12DeviceRemovedExtendedData1>( dredPtr );

	// Последняя выполненная команда каждого списка: op — D3D12_AUTO_BREADCRUMB_OP (например, 12 — Dispatch, 20 — DrawIndexedInstanced)
	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
	if( SUCCEEDED( dred->GetAutoBreadcrumbsOutput1( &breadcrumbs ) ) )
	{
		for( const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode; node; node = node->pNext )
		{
			if( !node->pLastBreadcrumbValue || *node->pLastBreadcrumbValue >= node->BreadcrumbCount )
				continue;
			const UINT last = *node->pLastBreadcrumbValue;
			LOG( std::string( "DRED: command list " ) + ( node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "?" ) +
				 " stopped at command " + std::to_string( last ) + " of " + std::to_string( node->BreadcrumbCount ) +
				 ", op " + std::to_string( static_cast<int>( node->pCommandHistory[last] ) ) );
		}
	}
	D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
	if( SUCCEEDED( dred->GetPageFaultAllocationOutput1( &pageFault ) ) && pageFault.PageFaultVA )
		LOG( "DRED: page fault at GPU address " + std::to_string( pageFault.PageFaultVA ) );
}

bool DMD3D::resize( uint32_t width, uint32_t height )
{
	if( !m_swapChain || width == 0 || height == 0 )
		return false;
	if( width == m_screenWidth && height == m_screenHeight )
		return true;

	// GPU закончил с задними буферами → ссылок на них нет → ResizeBuffers → цели заново. Буфер сцены — того же
	// размера, что задний буфер: отпускается здесь, создаётся при следующем BeginScene
	waitForGpu();
	releaseBackBufferTargets();
	releaseSceneTargets();
	const HRESULT hr = m_swapChain->ResizeBuffers( backBufferCount, width, height, backBufferFormat, m_swapChainFlags );
	if( FAILED( hr ) )
	{
		LOG( "ResizeBuffers " + std::to_string( width ) + "x" + std::to_string( height ) + " failed, HRESULT " + std::to_string( static_cast<long>( hr ) ) );
		return false;
	}
	m_screenWidth = width;
	m_screenHeight = height;
	if( !createBackBufferTargets() )
		return false;
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
	LOG( "Back buffer resized to " + std::to_string( width ) + "x" + std::to_string( height ) );
	return true;
}

void DMD3D::Shutdown()
{
	if( !m_device )
		return;

	waitForGpu();
	if( m_recording )
	{
		m_commandList->Close();
		m_recording = false;
	}
	if( m_frameLatencyWaitable )
	{
		CloseHandle( m_frameLatencyWaitable );
		m_frameLatencyWaitable = nullptr;
	}
	if( m_swapChain )
		m_swapChain->SetFullscreenState( false, nullptr );
	savePipelineLibrary();

	// Виды и ресурсы — до куч дескрипторов и аллокатора: отложенные отпускаются сразу (GPU остановлен)
	releaseBackBufferTargets();
	releaseSceneTargets();
	for( FrameResources& frame : m_frames )
		processDeferred( frame );
	m_constantRing = ConstantRing();
	if( m_infoQueue && m_messageCookie )
	{
		m_infoQueue->UnregisterMessageCallback( m_messageCookie );
		m_messageCookie = 0;
	}
	if( m_fenceEvent )
	{
		CloseHandle( m_fenceEvent );
		m_fenceEvent = nullptr;
	}
	m_swapChain.reset();
	m_pipelines.clear();
	m_computePipelines.clear();
	m_pipelineLibrary.reset();
	m_pipelineLibraryData.clear();
	m_rootSignature.reset();
	m_commandList.reset();
	for( FrameResources& frame : m_frames )
		frame.allocator.reset();
	m_fence.reset();
	if( m_allocator )
	{
		m_allocator->Release();
		m_allocator = nullptr;
	}
	m_infoQueue.reset();
	m_device.reset();
}

// ---------------------------------------------------------------------------------------------------------------------
// Состояния и пайплайны
// ---------------------------------------------------------------------------------------------------------------------

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

const Pipeline& DMD3D::pipeline( const PipelineDesc& desc )
{
	const uint64_t key = desc.key();
	auto found = m_pipelines.find( key );
	if( found != m_pipelines.end() )
		return found->second;

	const uint32_t id = static_cast<uint32_t>( m_pipelines.size() );
	if( m_pipelinesWarm )
	{
		// Не из списка прогрева: сборка PSO в кадре — фриз; дополнить прогрев (DMShader::warmPipelines)
		++m_lazyPipelines;
		LOG( "Pipeline " + std::to_string( id ) + " is created lazily: raster " + std::to_string( static_cast<int>( desc.state.raster ) ) +
			 ", depth " + std::to_string( static_cast<int>( desc.state.depth ) ) + ", blend " + std::to_string( static_cast<int>( desc.state.blend ) ) +
			 ", topology " + std::to_string( static_cast<int>( desc.topology ) ) );
	}
	Pipeline& pipeline = m_pipelines.emplace( key, Pipeline( desc, id ) ).first->second;
	createPipelineObject( pipeline );
	return pipeline;
}

void DMD3D::setPipeline( const Pipeline& pipeline )
{
	m_renderState = pipeline.desc().state;
	if( ID3D12PipelineState* object = pipeline.object() )
	{
		m_commandList->SetPipelineState( object );
		m_commandList->SetGraphicsRootSignature( m_rootSignature.get() );
		m_commandList->IASetPrimitiveTopology( pipeline.desc().topology );
	}
}

void DMD3D::setShaderStage( SRVType type, const ShaderStage* stage )
{
	if( type != SRVType::cs || !stage || !stage->valid() )
		return;
	if( ID3D12PipelineState* object = computePipeline( *stage ) )
	{
		m_commandList->SetPipelineState( object );
		m_commandList->SetComputeRootSignature( m_rootSignature.get() );
	}
}

bool DMD3D::setShadowSlopeBias( float slopeBias )
{
	if( m_shadowSlopeBias == slopeBias && !m_pipelines.empty() )
		return true;
	m_shadowSlopeBias = slopeBias;
	// Смещение — в растеризаторе пайплайнов теней: они собираются заново на месте (без счёта «ленивых»)
	bool ok = true;
	for( auto& [key, pipeline] : m_pipelines )
	{
		if( pipeline.desc().state.raster == RasterState::csmShadowDepth )
			ok = createPipelineObject( pipeline ) && ok;
	}
	return ok;
}

TargetFormats DMD3D::sceneFormats()
{
	return TargetFormats::colorTarget( sceneColorFormat, sceneDepthFormat );
}

TargetFormats DMD3D::depthOnlyFormats()
{
	return TargetFormats::depthTarget( sceneDepthFormat );
}

TargetFormats DMD3D::backBufferFormats()
{
	return TargetFormats::colorTarget( backBufferViewFormat );
}

bool DMD3D::createRootSignature()
{
	// [0] — root-константы таблицы привязок вызова (b8, DM_BINDING_COUNT DWORD: индексы дескрипторов по слотам —
	// Shaders/bindless.sh); [1…SLOT_CB_COUNT] — root CBV b0…b7 (кольцо констант и буферы проходов); сэмплеры статические
	DMSamplerState samplers;
	samplers.initialize();
	D3D12_ROOT_PARAMETER1 parameters[1 + SLOT_CB_COUNT] = {};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[0].Constants.ShaderRegister = SLOT_CB_BINDINGS;
	parameters[0].Constants.Num32BitValues = DM_BINDING_COUNT;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	for( uint32_t i = 0; i < SLOT_CB_COUNT; ++i )
	{
		parameters[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		parameters[1 + i].Descriptor.ShaderRegister = i;
		parameters[1 + i].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		parameters[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}
	D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
	desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	desc.Desc_1_1.NumParameters = static_cast<UINT>( std::size( parameters ) );
	desc.Desc_1_1.pParameters = parameters;
	desc.Desc_1_1.NumStaticSamplers = static_cast<UINT>( samplers.staticSamplers().size() );
	desc.Desc_1_1.pStaticSamplers = samplers.staticSamplers().data();
	desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

	ID3DBlob* blob = nullptr;
	ID3DBlob* error = nullptr;
	if( FAILED( D3D12SerializeVersionedRootSignature( &desc, &blob, &error ) ) )
	{
		LOG( std::string( "Root signature serialization failed: " ) + ( error ? static_cast<const char*>( error->GetBufferPointer() ) : "" ) );
		if( error )
			error->Release();
		return false;
	}
	ID3D12RootSignature* rootSignature = nullptr;
	const HRESULT hr = m_device->CreateRootSignature( 0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof( ID3D12RootSignature ),
													  reinterpret_cast<void**>( &rootSignature ) );
	blob->Release();
	if( FAILED( hr ) )
	{
		LOG( "CreateRootSignature failed" );
		return false;
	}
	rootSignature->SetName( L"Root signature" );
	m_rootSignature = make_com_ptr<ID3D12RootSignature>( rootSignature );
	return true;
}

std::wstring DMD3D::pipelineName( const PipelineDesc& desc )
{
	uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash]( uint64_t value )
	{
		for( int i = 0; i < 8; ++i )
		{
			hash ^= ( value >> ( i * 8 ) ) & 0xFF;
			hash *= 1099511628211ull;
		}
	};
	const auto mixText = [&hash]( const char* text )
	{
		for( ; text && *text; ++text )
		{
			hash ^= static_cast<uint8_t>( *text );
			hash *= 1099511628211ull;
		}
	};
	mix( desc.vertex ? desc.vertex->hash() : 0 );
	mix( desc.pixel ? desc.pixel->hash() : 0 );
	mix( desc.geometry ? desc.geometry->hash() : 0 );
	if( desc.layout )
	{
		for( const VertexElement& element : desc.layout->elements() )
		{
			mixText( element.semantic );
			mix( static_cast<uint64_t>( element.semanticIndex ) | static_cast<uint64_t>( element.format ) << 8 |
				 static_cast<uint64_t>( element.slot ) << 24 | static_cast<uint64_t>( element.offset ) << 32 | ( element.perInstance ? 1ull << 63 : 0 ) );
		}
	}
	mix( static_cast<uint64_t>( desc.state.raster ) | static_cast<uint64_t>( desc.state.depth ) << 8 | static_cast<uint64_t>( desc.state.blend ) << 16 |
		 static_cast<uint64_t>( desc.topology ) << 24 );
	for( uint32_t i = 0; i < desc.formats.colorCount; ++i )
		mix( static_cast<uint64_t>( desc.formats.color[i] ) | static_cast<uint64_t>( i ) << 32 );
	mix( static_cast<uint64_t>( desc.formats.depth ) | static_cast<uint64_t>( desc.formats.colorCount ) << 32 );
	wchar_t name[24];
	swprintf_s( name, L"p%016llx", static_cast<unsigned long long>( hash ) );
	return name;
}

bool DMD3D::createPipelineObject( Pipeline& pipeline )
{
	const PipelineDesc& desc = pipeline.desc();
	pipeline.setObject( nullptr );
	if( !desc.vertex || !desc.vertex->valid() )
	{
		LOG( "Pipeline " + std::to_string( pipeline.id() ) + ": no vertex shader" );
		return false;
	}

	// Растеризатор по RasterState (как состояния D3D11 до переезда)
	CD3DX12_RASTERIZER_DESC rasterizer( D3D12_DEFAULT );
	switch( desc.state.raster )
	{
		case RasterState::solid: rasterizer.CullMode = D3D12_CULL_MODE_BACK; break;
		case RasterState::frontCulling: rasterizer.CullMode = D3D12_CULL_MODE_FRONT; break;
		case RasterState::noCulling: rasterizer.CullMode = D3D12_CULL_MODE_NONE; break;
		case RasterState::wireframe:
			rasterizer.FillMode = D3D12_FILL_MODE_WIREFRAME;
			rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			break;
		// Зеркальные меши: лицевые грани — против часовой стрелки
		case RasterState::solidMirrored:
			rasterizer.CullMode = D3D12_CULL_MODE_BACK;
			rasterizer.FrontCounterClockwise = TRUE;
			break;
		case RasterState::noCullingMirrored:
			rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			rasterizer.FrontCounterClockwise = TRUE;
			break;
		// Глубина каскадов теней: без отсечения граней (рельеф, тонкие панели и лепестки тоже отбрасывают тень), наклонное
		// смещение от света (глубина обратная — знак минус), без отсечения по глубине (pancaking)
		case RasterState::csmShadowDepth:
			rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			rasterizer.SlopeScaledDepthBias = -m_shadowSlopeBias;
			rasterizer.DepthBiasClamp = -0.01f;
			rasterizer.DepthClipEnable = FALSE;
			break;
	}

	// Глубина обратная: «ближе» — GREATER. Без цели глубины проверка выключена, что бы ни просило состояние
	CD3DX12_DEPTH_STENCIL_DESC depth( D3D12_DEFAULT );
	depth.StencilEnable = FALSE;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	switch( desc.state.depth )
	{
		case DepthState::enabled: depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; break;
		case DepthState::readOnly: depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; break;
		case DepthState::readOnlyNearOrEqual:
			depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			break;
		case DepthState::readOnlyEqual:
			depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
			break;
		case DepthState::disabled: depth.DepthEnable = FALSE; break;
	}
	if( desc.formats.depth == DXGI_FORMAT_UNKNOWN )
		depth.DepthEnable = FALSE;

	CD3DX12_BLEND_DESC blend( D3D12_DEFAULT );
	D3D12_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
	switch( desc.state.blend )
	{
		case BlendState::alpha:
			target.BlendEnable = TRUE;
			target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			target.SrcBlendAlpha = D3D12_BLEND_ONE;
			target.DestBlendAlpha = D3D12_BLEND_ZERO;
			break;
		case BlendState::additive:
			target.BlendEnable = TRUE;
			target.SrcBlend = D3D12_BLEND_ONE;
			target.DestBlend = D3D12_BLEND_ONE;
			target.SrcBlendAlpha = D3D12_BLEND_ONE;
			target.DestBlendAlpha = D3D12_BLEND_ONE;
			break;
		case BlendState::opaque: break;
	}

	std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
	if( desc.layout )
	{
		for( const VertexElement& element : desc.layout->elements() )
		{
			D3D12_INPUT_ELEMENT_DESC input = {};
			input.SemanticName = element.semantic;
			input.SemanticIndex = element.semanticIndex;
			input.Format = element.format;
			input.InputSlot = element.slot;
			input.AlignedByteOffset = element.offset == VertexElement::appendOffset ? D3D12_APPEND_ALIGNED_ELEMENT : element.offset;
			input.InputSlotClass = element.perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			input.InstanceDataStepRate = element.perInstance ? 1 : 0;
			elements.push_back( input );
		}
	}

	D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	if( desc.topology == D3D_PRIMITIVE_TOPOLOGY_POINTLIST )
		topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	else if( desc.topology >= D3D_PRIMITIVE_TOPOLOGY_LINELIST && desc.topology <= D3D_PRIMITIVE_TOPOLOGY_LINESTRIP ||
			 desc.topology == D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ || desc.topology == D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ )
		topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	else if( desc.topology >= D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST )
		topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

	D3D12_RT_FORMAT_ARRAY renderTargets = {};
	renderTargets.NumRenderTargets = desc.formats.colorCount;
	for( uint32_t i = 0; i < desc.formats.colorCount; ++i )
		renderTargets.RTFormats[i] = desc.formats.color[i];

	struct Stream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_VS vs;
		CD3DX12_PIPELINE_STATE_STREAM_PS ps;
		CD3DX12_PIPELINE_STATE_STREAM_GS gs;
		CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencil;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY topology;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS renderTargets;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT depthFormat;
		CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC sampleDesc;
		CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK sampleMask;
	} stream;
	stream.rootSignature = m_rootSignature.get();
	stream.vs = D3D12_SHADER_BYTECODE{ desc.vertex->data(), desc.vertex->size() };
	if( desc.pixel && desc.pixel->valid() )
		stream.ps = D3D12_SHADER_BYTECODE{ desc.pixel->data(), desc.pixel->size() };
	if( desc.geometry && desc.geometry->valid() )
		stream.gs = D3D12_SHADER_BYTECODE{ desc.geometry->data(), desc.geometry->size() };
	stream.blend = blend;
	stream.rasterizer = rasterizer;
	stream.depthStencil = depth;
	stream.inputLayout = D3D12_INPUT_LAYOUT_DESC{ elements.empty() ? nullptr : elements.data(), static_cast<UINT>( elements.size() ) };
	stream.topology = topologyType;
	stream.renderTargets = renderTargets;
	stream.depthFormat = desc.formats.depth;
	stream.sampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };
	stream.sampleMask = UINT_MAX;
	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = { sizeof( stream ), &stream };

	// Из библиотеки на диске по имени-хэшу; нет — собрать и положить
	const std::wstring name = pipelineName( desc );
	ID3D12PipelineState* object = nullptr;
	HRESULT hr = E_FAIL;
	if( m_pipelineLibrary )
		hr = m_pipelineLibrary->LoadPipeline( name.c_str(), &streamDesc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
	if( FAILED( hr ) )
	{
		hr = m_device->CreatePipelineState( &streamDesc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
		if( FAILED( hr ) )
		{
			LOG( "CreatePipelineState failed for pipeline " + std::to_string( pipeline.id() ) + ": raster " + std::to_string( static_cast<int>( desc.state.raster ) ) +
				 ", depth " + std::to_string( static_cast<int>( desc.state.depth ) ) + ", blend " + std::to_string( static_cast<int>( desc.state.blend ) ) +
				 ", colors " + std::to_string( desc.formats.colorCount ) + ", depth format " + std::to_string( desc.formats.depth ) +
				 ", HRESULT " + std::to_string( static_cast<long>( hr ) ) );
			return false;
		}
		if( m_pipelineLibrary && SUCCEEDED( m_pipelineLibrary->StorePipeline( name.c_str(), object ) ) )
			m_pipelineLibraryDirty = true;
	}
	object->SetName( name.c_str() );
	pipeline.setObject( object );
	return true;
}

ID3D12PipelineState* DMD3D::computePipeline( const ShaderStage& stage )
{
	auto found = m_computePipelines.find( stage.hash() );
	if( found != m_computePipelines.end() )
		return found->second.get();

	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_rootSignature.get();
	desc.CS = D3D12_SHADER_BYTECODE{ stage.data(), stage.size() };
	wchar_t name[24];
	swprintf_s( name, L"c%016llx", static_cast<unsigned long long>( stage.hash() ) );
	ID3D12PipelineState* object = nullptr;
	HRESULT hr = E_FAIL;
	if( m_pipelineLibrary )
		hr = m_pipelineLibrary->LoadComputePipeline( name, &desc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
	if( FAILED( hr ) )
	{
		hr = m_device->CreateComputePipelineState( &desc, __uuidof( ID3D12PipelineState ), reinterpret_cast<void**>( &object ) );
		if( FAILED( hr ) )
		{
			LOG( "CreateComputePipelineState failed, HRESULT " + std::to_string( static_cast<long>( hr ) ) );
			m_computePipelines.emplace( stage.hash(), nullptr );
			return nullptr;
		}
		if( m_pipelineLibrary && SUCCEEDED( m_pipelineLibrary->StorePipeline( name, object ) ) )
			m_pipelineLibraryDirty = true;
	}
	object->SetName( name );
	if( m_pipelinesWarm )
	{
		++m_lazyPipelines;
		LOG( "Compute pipeline is created lazily (DMComputeShader::Initialize warms it)" );
	}
	return m_computePipelines.emplace( stage.hash(), make_com_ptr<ID3D12PipelineState>( object ) ).first->second.get();
}

bool DMD3D::loadPipelineLibrary()
{
	// Кэш PSO на диске — ID3D12PipelineLibrary: сборка пайплайнов при загрузке уровня из него почти бесплатна. От другого
	// драйвера или адаптера рантайм её не принимает — тогда библиотека пустая и пересобирается
	m_pipelineLibraryData.clear();
	std::ifstream file( "cache/pipelines.bin", std::ios::binary );
	if( file )
		m_pipelineLibraryData.assign( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
	ID3D12PipelineLibrary1* library = nullptr;
	HRESULT hr = m_device->CreatePipelineLibrary( m_pipelineLibraryData.data(), m_pipelineLibraryData.size(), __uuidof( ID3D12PipelineLibrary1 ),
													  reinterpret_cast<void**>( &library ) );
	if( FAILED( hr ) && !m_pipelineLibraryData.empty() )
	{
		LOG( "Pipeline library cache/pipelines.bin is not accepted (driver or adapter changed), rebuilding" );
		m_pipelineLibraryData.clear();
		hr = m_device->CreatePipelineLibrary( nullptr, 0, __uuidof( ID3D12PipelineLibrary1 ), reinterpret_cast<void**>( &library ) );
	}
	if( FAILED( hr ) )
	{
		LOG( "CreatePipelineLibrary failed: pipelines are built without the disk cache" );
		return false;
	}
	m_pipelineLibrary = make_com_ptr<ID3D12PipelineLibrary1>( library );
	m_pipelineLibraryDirty = false;
	if( !m_pipelineLibraryData.empty() )
		LOG( "Pipeline library: " + std::to_string( m_pipelineLibraryData.size() / 1024 ) + " KB from cache/pipelines.bin" );
	return true;
}

void DMD3D::savePipelineLibrary()
{
	if( !m_pipelineLibrary || !m_pipelineLibraryDirty )
		return;
	const SIZE_T size = m_pipelineLibrary->GetSerializedSize();
	std::vector<uint8_t> data( size );
	if( size == 0 || FAILED( m_pipelineLibrary->Serialize( data.data(), size ) ) )
		return;
	std::error_code error;
	std::filesystem::create_directories( "cache", error );
	std::ofstream file( "cache/pipelines.bin", std::ios::binary );
	if( file )
	{
		file.write( reinterpret_cast<const char*>( data.data() ), static_cast<std::streamsize>( size ) );
		LOG( "Pipeline library saved: " + std::to_string( size / 1024 ) + " KB, " + std::to_string( m_pipelines.size() ) + " graphics and " +
			 std::to_string( m_computePipelines.size() ) + " compute pipelines" );
	}
	m_pipelineLibraryDirty = false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ресурсы
// ---------------------------------------------------------------------------------------------------------------------

void DMD3D::notImplemented( const char* method )
{
	if( m_notImplemented.insert( method ).second )
		LOG( std::string( "DMD3D::" ) + method + " is not implemented yet (D3D12 port in progress)" );
}

bool DMD3D::createResource( const D3D12_RESOURCE_DESC1& desc, D3D12_HEAP_TYPE heap, D3D12_BARRIER_LAYOUT initialLayout,
							const D3D12_CLEAR_VALUE* clearValue, ID3D12Resource** resource, D3D12MA::Allocation** allocation )
{
	D3D12MA::ALLOCATION_DESC allocationDesc = {};
	allocationDesc.HeapType = heap;
	const HRESULT hr = m_allocator->CreateResource3( &allocationDesc, &desc, initialLayout, clearValue, 0, nullptr, allocation,
													 __uuidof( ID3D12Resource ), reinterpret_cast<void**>( resource ) );
	if( FAILED( hr ) )
	{
		LOG( "CreateResource failed, HRESULT " + std::to_string( static_cast<long>( hr ) ) + ": " +
			 ( desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? "buffer of " + std::to_string( desc.Width ) + " bytes" :
			   "texture " + std::to_string( desc.Width ) + "x" + std::to_string( desc.Height ) + ", format " + std::to_string( desc.Format ) ) );
		return false;
	}
	ResourceState state;
	state.layout = initialLayout;
	state.texture = desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER;
	m_states[*resource] = state;
	return true;
}

bool DMD3D::createStaging( uint64_t bytes, D3D12_HEAP_TYPE heap, ID3D12Resource** resource, D3D12MA::Allocation** allocation, void** mapped )
{
	D3D12_RESOURCE_DESC1 desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = std::max<uint64_t>( bytes, 1 );
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if( !createResource( desc, heap, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, resource, allocation ) )
		return false;
	( *resource )->SetName( heap == D3D12_HEAP_TYPE_UPLOAD ? L"Upload staging" : L"Readback staging" );
	if( mapped )
	{
		const D3D12_RANGE noRead = { 0, 0 };
		if( FAILED( ( *resource )->Map( 0, heap == D3D12_HEAP_TYPE_UPLOAD ? &noRead : nullptr, mapped ) ) )
			return false;
	}
	return true;
}

bool DMD3D::uploadToBuffer( ID3D12Resource* destination, uint64_t destinationOffset, const void* data, uint64_t size )
{
	if( !data || size == 0 )
		return true;
	barrier( destination, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_UNDEFINED, true );

	// В кадре небольшие данные идут через участок кольца (он живёт, пока GPU не закончит кадр); до первого кадра и
	// большие — отдельный upload-буфер, отпускается, когда GPU закончит копирование
	uint32_t offset = 0, bytes = 0;
	if( m_frameStarted && !m_writingBuffer && size <= constantRingBytes / frameCount / 4 )
	{
		if( void* slice = m_constantRing.beginWrite( static_cast<uint32_t>( size ), offset, bytes ) )
		{
			memcpy( slice, data, size );
			m_constantRing.finishWrite();
			flushBarriers();
			m_commandList->CopyBufferRegion( destination, destinationOffset, m_constantRing.handle(), offset, size );
			return true;
		}
	}
	ID3D12Resource* staging = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	void* mapped = nullptr;
	if( !createStaging( size, D3D12_HEAP_TYPE_UPLOAD, &staging, &allocation, &mapped ) )
		return false;
	memcpy( mapped, data, size );
	staging->Unmap( 0, nullptr );
	flushBarriers();
	m_commandList->CopyBufferRegion( destination, destinationOffset, staging, 0, size );
	m_states.erase( staging );
	deferRelease( staging );
	deferRelease( allocation );
	return true;
}

bool DMD3D::uploadToTexture( ID3D12Resource* destination, const TextureDesc& desc, const TextureData* initial )
{
	const uint32_t count = subresourceCount( desc );
	const D3D12_RESOURCE_DESC resourceDesc = destination->GetDesc();
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts( count );
	std::vector<UINT> rows( count );
	std::vector<UINT64> rowBytes( count );
	UINT64 total = 0;
	m_device->GetCopyableFootprints( &resourceDesc, 0, count, 0, layouts.data(), rows.data(), rowBytes.data(), &total );

	ID3D12Resource* staging = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	void* mapped = nullptr;
	if( !createStaging( total, D3D12_HEAP_TYPE_UPLOAD, &staging, &allocation, &mapped ) )
		return false;

	// Подресурс i у D3D12 — мип + срез × мипов: тот же порядок, что у TextureData (срез за срезом, внутри среза мипы)
	for( uint32_t i = 0; i < count; ++i )
	{
		const TextureData& source = initial[i];
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[i];
		const uint32_t sourceRowPitch = source.rowPitch ? source.rowPitch : static_cast<uint32_t>( rowBytes[i] );
		const uint32_t sourceSlicePitch = source.slicePitch ? source.slicePitch : sourceRowPitch * rows[i];
		for( uint32_t z = 0; z < layout.Footprint.Depth; ++z )
		{
			for( uint32_t row = 0; row < rows[i]; ++row )
			{
				uint8_t* target = static_cast<uint8_t*>( mapped ) + layout.Offset +
								  ( static_cast<uint64_t>( z ) * rows[i] + row ) * layout.Footprint.RowPitch;
				const uint8_t* pixels = static_cast<const uint8_t*>( source.data ) + static_cast<uint64_t>( z ) * sourceSlicePitch +
										static_cast<uint64_t>( row ) * sourceRowPitch;
				memcpy( target, pixels, rowBytes[i] );
			}
		}
	}
	staging->Unmap( 0, nullptr );

	barrier( destination, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_COPY_DEST );
	flushBarriers();
	for( uint32_t i = 0; i < count; ++i )
	{
		D3D12_TEXTURE_COPY_LOCATION target = {};
		target.pResource = destination;
		target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		target.SubresourceIndex = i;
		D3D12_TEXTURE_COPY_LOCATION source = {};
		source.pResource = staging;
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = layouts[i];
		m_commandList->CopyTextureRegion( &target, 0, 0, 0, &source, nullptr );
	}
	// Дальше текстуру читают шейдеры (и GUI)
	barrier( destination, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE );
	m_states.erase( staging );
	deferRelease( staging );
	deferRelease( allocation );
	return true;
}

bool DMD3D::createBuffer( const BufferDesc& desc, const void* initialData, Buffer& buffer )
{
	// Константы кадра — участок кольца, своего ресурса нет: данные появятся при первой записи
	buffer.reset( nullptr, nullptr, desc );
	if( buffer.ring() )
		return true;

	D3D12_RESOURCE_DESC1 resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	// CBV — участками по 256 байт
	resourceDesc.Width = ( desc.usage & BufferUsage::constant ) ? ( desc.size + 255 ) & ~255u : std::max( desc.size, 1u );
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = ( desc.usage & BufferUsage::unorderedAccess ) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
	const bool readback = ( desc.usage & BufferUsage::readback ) != 0;

	ID3D12Resource* resource = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	if( !createResource( resourceDesc, readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr,
						 &resource, &allocation ) )
		return false;
	buffer.reset( resource, allocation, desc );
	if( initialData && !readback )
	{
		if( !uploadToBuffer( resource, 0, initialData, desc.size ) )
			return false;
		barrier( resource, steadyAccess( desc.usage ) );
	}
	return true;
}

bool DMD3D::createShaderConstantBuffer( size_t byteSize, Buffer& buffer )
{
	BufferDesc desc;
	desc.size = static_cast<uint32_t>( byteSize );
	desc.usage = BufferUsage::constant | BufferUsage::cpuWrite;
	return createBuffer( desc, nullptr, buffer );
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
	view.reset();
	if( !buffer.handle() )
		return false;
	const bool structured = ( buffer.desc().usage & BufferUsage::structured ) != 0 && buffer.desc().stride;
	const uint32_t elementSize = desc.raw || !structured ? 4 : buffer.desc().stride;
	D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.raw || !structured ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	viewDesc.Buffer.FirstElement = desc.firstElement;
	viewDesc.Buffer.NumElements = desc.elementCount ? desc.elementCount : buffer.size() / elementSize - desc.firstElement;
	viewDesc.Buffer.StructureByteStride = desc.raw || !structured ? 0 : buffer.desc().stride;
	viewDesc.Buffer.Flags = desc.raw || !structured ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
	const Descriptor descriptor = allocateShaderDescriptor();
	if( !descriptor.valid() )
		return false;
	m_device->CreateShaderResourceView( buffer.handle(), &viewDesc, descriptor.cpu );
	view.reset( descriptor, buffer.handle() );
	return true;
}

bool DMD3D::createStorageView( const Buffer& buffer, const BufferViewDesc& desc, StorageView& view )
{
	view.reset();
	if( !buffer.handle() )
		return false;
	const bool structured = ( buffer.desc().usage & BufferUsage::structured ) != 0 && buffer.desc().stride;
	const uint32_t elementSize = desc.raw || !structured ? 4 : buffer.desc().stride;
	D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.raw || !structured ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	viewDesc.Buffer.FirstElement = desc.firstElement;
	viewDesc.Buffer.NumElements = desc.elementCount ? desc.elementCount : buffer.size() / elementSize - desc.firstElement;
	viewDesc.Buffer.StructureByteStride = desc.raw || !structured ? 0 : buffer.desc().stride;
	viewDesc.Buffer.Flags = desc.raw || !structured ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
	const Descriptor descriptor = allocateShaderDescriptor();
	const Descriptor clearDescriptor = m_stagingHeap.allocate();
	if( !descriptor.valid() || !clearDescriptor.valid() )
	{
		LOG( "Descriptor heap is full (UAV)" );
		return false;
	}
	m_device->CreateUnorderedAccessView( buffer.handle(), nullptr, &viewDesc, descriptor.cpu );
	m_device->CreateUnorderedAccessView( buffer.handle(), nullptr, &viewDesc, clearDescriptor.cpu );
	view.reset( descriptor, clearDescriptor, buffer.handle() );
	return true;
}

bool DMD3D::createTexture( const TextureDesc& desc, const TextureData* initial, Texture& texture )
{
	return createTextureInternal( desc, initial, texture, nullptr, nullptr );
}

bool DMD3D::createTextureInternal( const TextureDesc& desc, const TextureData* initial, Texture& texture, const float* clearColor,
									const wchar_t* name )
{
	texture.reset();
	D3D12_RESOURCE_DESC1 resourceDesc = {};
	resourceDesc.Dimension = desc.depth > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = desc.width;
	resourceDesc.Height = desc.height;
	resourceDesc.DepthOrArraySize = static_cast<UINT16>( desc.depth > 1 ? desc.depth : desc.arraySize );
	resourceDesc.MipLevels = static_cast<UINT16>( desc.mipCount );	// 0 — полная цепочка
	resourceDesc.Format = desc.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	if( desc.usage & TextureUsage::renderTarget )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if( desc.usage & TextureUsage::depthStencil )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if( desc.usage & TextureUsage::unorderedAccess )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if( ( desc.usage & TextureUsage::depthStencil ) && !( desc.usage & TextureUsage::shaderResource ) )
		resourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

	// Начальный layout — по назначению; текстуры с данными — COMMON (первый барьер — в COPY_DEST). Optimized clear
	// value: у глубины — 0 (обратная глубина), у цветной цели — цвет, если задан (буфер сцены)
	D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_COMMON;
	D3D12_CLEAR_VALUE clearValue = {};
	const D3D12_CLEAR_VALUE* clear = nullptr;
	if( desc.usage & TextureUsage::depthStencil )
	{
		layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		clearValue.Format = depthFormatFor( desc.format );
		clearValue.DepthStencil.Depth = 0.0f;
		clear = &clearValue;
	}
	else if( desc.usage & TextureUsage::renderTarget )
	{
		layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
		if( clearColor )
		{
			clearValue.Format = desc.format;
			memcpy( clearValue.Color, clearColor, sizeof( clearValue.Color ) );
			clear = &clearValue;
		}
	}
	else if( desc.usage & TextureUsage::unorderedAccess )
		layout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;

	ID3D12Resource* resource = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	if( !createResource( resourceDesc, D3D12_HEAP_TYPE_DEFAULT, layout, clear, &resource, &allocation ) )
		return false;
	if( name )
		resource->SetName( name );

	TextureDesc created = desc;
	created.mipCount = resource->GetDesc().MipLevels;
	texture.reset( resource, allocation, created );
	if( initial )
		return uploadToTexture( resource, created, initial );
	return true;
}

bool DMD3D::createShaderView( const Texture& texture, const TextureViewDesc& desc, ShaderView& view )
{
	view.reset();
	if( !texture.handle() )
		return false;
	const TextureDesc& textureDesc = texture.desc();
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	const uint32_t mipCount = desc.mipCount ? desc.mipCount : textureDesc.mipCount - desc.firstMip;
	const uint32_t sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	switch( kind )
	{
		case TextureViewDesc::Kind::texture3D:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			viewDesc.Texture3D.MostDetailedMip = desc.firstMip;
			viewDesc.Texture3D.MipLevels = mipCount;
			break;
		case TextureViewDesc::Kind::cube:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			viewDesc.TextureCube.MostDetailedMip = desc.firstMip;
			viewDesc.TextureCube.MipLevels = mipCount;
			break;
		case TextureViewDesc::Kind::texture2DArray:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			viewDesc.Texture2DArray.MostDetailedMip = desc.firstMip;
			viewDesc.Texture2DArray.MipLevels = mipCount;
			viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
			viewDesc.Texture2DArray.ArraySize = sliceCount;
			break;
		default:
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MostDetailedMip = desc.firstMip;
			viewDesc.Texture2D.MipLevels = mipCount;
			break;
	}
	const Descriptor descriptor = allocateShaderDescriptor();
	if( !descriptor.valid() )
		return false;
	m_device->CreateShaderResourceView( texture.handle(), &viewDesc, descriptor.cpu );
	view.reset( descriptor, texture.handle(), { desc.firstMip, mipCount, desc.firstSlice, sliceCount } );
	return true;
}

bool DMD3D::createTargetView( const Texture& texture, const TextureViewDesc& desc, TargetView& view )
{
	view.reset();
	if( !texture.handle() )
		return false;
	const TextureDesc& textureDesc = texture.desc();
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	const uint32_t sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	DXGI_FORMAT format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	if( textureDesc.usage & TextureUsage::depthStencil )
		format = depthFormatFor( format );

	if( isDepthFormat( format ) )
	{
		const Descriptor descriptor = m_dsvHeap.allocate();
		if( !descriptor.valid() )
		{
			LOG( "DSV descriptor heap is full" );
			return false;
		}
		D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
		viewDesc.Format = format;
		if( kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube )
		{
			viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			viewDesc.Texture2DArray.MipSlice = desc.firstMip;
			viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
			viewDesc.Texture2DArray.ArraySize = sliceCount;
		}
		else
		{
			viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MipSlice = desc.firstMip;
		}
		m_device->CreateDepthStencilView( texture.handle(), &viewDesc, descriptor.cpu );
		view.reset( descriptor, true, format, texture.handle(), { desc.firstMip, 1, desc.firstSlice, sliceCount } );
		return true;
	}

	const Descriptor descriptor = m_rtvHeap.allocate();
	if( !descriptor.valid() )
	{
		LOG( "RTV descriptor heap is full" );
		return false;
	}
	D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
	viewDesc.Format = format;
	if( kind == TextureViewDesc::Kind::texture3D )
	{
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
		viewDesc.Texture3D.MipSlice = desc.firstMip;
		viewDesc.Texture3D.FirstWSlice = desc.firstSlice;
		viewDesc.Texture3D.WSize = desc.sliceCount ? desc.sliceCount : static_cast<UINT>( -1 );
	}
	else if( kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube )
	{
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		viewDesc.Texture2DArray.MipSlice = desc.firstMip;
		viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
		viewDesc.Texture2DArray.ArraySize = sliceCount;
	}
	else
	{
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = desc.firstMip;
	}
	m_device->CreateRenderTargetView( texture.handle(), &viewDesc, descriptor.cpu );
	view.reset( descriptor, false, format, texture.handle(), { desc.firstMip, 1, desc.firstSlice, sliceCount } );
	return true;
}

bool DMD3D::createStorageView( const Texture& texture, const TextureViewDesc& desc, StorageView& view )
{
	view.reset();
	if( !texture.handle() )
		return false;
	const TextureDesc& textureDesc = texture.desc();
	const TextureViewDesc::Kind kind = resolveKind( textureDesc, desc );
	const uint32_t sliceCount = desc.sliceCount ? desc.sliceCount : textureDesc.arraySize - desc.firstSlice;
	D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
	viewDesc.Format = desc.format != DXGI_FORMAT_UNKNOWN ? desc.format : textureDesc.format;
	if( kind == TextureViewDesc::Kind::texture3D )
	{
		viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
		viewDesc.Texture3D.MipSlice = desc.firstMip;
		viewDesc.Texture3D.FirstWSlice = desc.firstSlice;
		viewDesc.Texture3D.WSize = desc.sliceCount ? desc.sliceCount : static_cast<UINT>( -1 );
	}
	else if( kind == TextureViewDesc::Kind::texture2DArray || kind == TextureViewDesc::Kind::cube )
	{
		viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		viewDesc.Texture2DArray.MipSlice = desc.firstMip;
		viewDesc.Texture2DArray.FirstArraySlice = desc.firstSlice;
		viewDesc.Texture2DArray.ArraySize = sliceCount;
	}
	else
	{
		viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = desc.firstMip;
	}
	const Descriptor descriptor = allocateShaderDescriptor();
	const Descriptor clearDescriptor = m_stagingHeap.allocate();
	if( !descriptor.valid() || !clearDescriptor.valid() )
	{
		LOG( "Descriptor heap is full (texture UAV)" );
		return false;
	}
	m_device->CreateUnorderedAccessView( texture.handle(), nullptr, &viewDesc, descriptor.cpu );
	m_device->CreateUnorderedAccessView( texture.handle(), nullptr, &viewDesc, clearDescriptor.cpu );
	view.reset( descriptor, clearDescriptor, texture.handle(), { desc.firstMip, 1, desc.firstSlice, sliceCount } );
	return true;
}

bool DMD3D::createShaderStage( SRVType type, const void* bytecode, size_t size, ShaderStage& stage )
{
	if( !bytecode || size == 0 )
		return false;
	stage.reset( type, bytecode, size );
	return true;
}

bool DMD3D::createInputLayout( const std::vector<VertexElement>& elements, const void*, size_t, InputLayout& layout )
{
	layout.reset( elements );
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Обновление, копирование, чтение
// ---------------------------------------------------------------------------------------------------------------------

void DMD3D::updateBuffer( Buffer& buffer, const void* data, size_t size )
{
	if( !buffer.handle() || !data )
		return;
	if( uploadToBuffer( buffer.handle(), 0, data, std::min<size_t>( size, buffer.size() ) ) )
		barrier( buffer.handle(), steadyAccess( buffer.desc().usage ) );
}

void DMD3D::copyBuffer( Buffer& destination, const Buffer& source )
{
	if( !destination.handle() || !source.handle() )
		return;
	barrier( source.handle(), D3D12_BARRIER_ACCESS_COPY_SOURCE );
	// Копия за копией в тот же буфер (readback каждый кадр) — барьер и при том же доступе: порядок записей
	barrier( destination.handle(), D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_UNDEFINED, true );
	flushBarriers();
	m_commandList->CopyBufferRegion( destination.handle(), 0, source.handle(), 0, std::min( destination.size(), source.size() ) );
	if( destination.desc().usage & BufferUsage::readback )
		destination.setCopyFence( m_fenceValue + 1 );	// сигнал в конце этого кадра (EndScene) или waitForGpu
	else
		barrier( destination.handle(), steadyAccess( destination.desc().usage ) );
}

bool DMD3D::readBuffer( const Buffer& readback, void* data, size_t size )
{
	if( !readback.handle() || readback.copyFence() == 0 || m_fence->GetCompletedValue() < readback.copyFence() )
		return false;
	const D3D12_RANGE range = { 0, std::min<SIZE_T>( size, readback.size() ) };
	void* mapped = nullptr;
	if( FAILED( readback.handle()->Map( 0, &range, &mapped ) ) )
		return false;
	memcpy( data, mapped, range.End );
	const D3D12_RANGE noWrite = { 0, 0 };
	readback.handle()->Unmap( 0, &noWrite );
	return true;
}

bool DMD3D::captureTexture( const Texture& texture, std::vector<SubresourceCopy>& copies, std::vector<uint8_t>& bytes )
{
	if( !texture.handle() )
		return false;
	const uint32_t count = subresourceCount( texture.desc() );
	const D3D12_RESOURCE_DESC resourceDesc = texture.handle()->GetDesc();
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts( count );
	std::vector<UINT> rows( count );
	std::vector<UINT64> rowBytes( count );
	UINT64 total = 0;
	m_device->GetCopyableFootprints( &resourceDesc, 0, count, 0, layouts.data(), rows.data(), rowBytes.data(), &total );

	ID3D12Resource* readback = nullptr;
	D3D12MA::Allocation* allocation = nullptr;
	if( !createStaging( total, D3D12_HEAP_TYPE_READBACK, &readback, &allocation, nullptr ) )
		return false;

	barrier( texture.handle(), D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_LAYOUT_COPY_SOURCE );
	flushBarriers();
	for( uint32_t i = 0; i < count; ++i )
	{
		D3D12_TEXTURE_COPY_LOCATION target = {};
		target.pResource = readback;
		target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		target.PlacedFootprint = layouts[i];
		D3D12_TEXTURE_COPY_LOCATION source = {};
		source.pResource = texture.handle();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		source.SubresourceIndex = i;
		m_commandList->CopyTextureRegion( &target, 0, 0, 0, &source, nullptr );
	}
	barrier( texture.handle(), D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE );
	waitForGpu();

	bool ok = false;
	void* mapped = nullptr;
	const D3D12_RANGE range = { 0, static_cast<SIZE_T>( total ) };
	if( SUCCEEDED( readback->Map( 0, &range, &mapped ) ) )
	{
		bytes.assign( static_cast<const uint8_t*>( mapped ), static_cast<const uint8_t*>( mapped ) + total );
		const D3D12_RANGE noWrite = { 0, 0 };
		readback->Unmap( 0, &noWrite );
		copies.resize( count );
		for( uint32_t i = 0; i < count; ++i )
			copies[i] = { layouts[i].Offset, layouts[i].Footprint.RowPitch, rows[i], rowBytes[i] };
		ok = true;
	}
	m_states.erase( readback );
	readback->Release();
	allocation->Release();
	return ok;
}

void* DMD3D::beginWrite( Buffer& buffer, uint32_t size )
{
	if( m_writingBuffer || size == 0 )
		return nullptr;
	if( !buffer.ring() && !buffer.handle() )
		return nullptr;
	uint32_t offset = 0, bytes = 0;
	void* data = m_constantRing.beginWrite( size, offset, bytes );
	if( !data )
		return nullptr;
	if( buffer.ring() )
		buffer.setRingSlice( offset, bytes );
	m_writingBuffer = &buffer;
	m_writeOffset = offset;
	m_writeBytes = std::min( size, buffer.size() );
	return data;
}

void DMD3D::endWrite()
{
	if( !m_writingBuffer )
		return;
	Buffer& buffer = *m_writingBuffer;
	m_writingBuffer = nullptr;
	m_constantRing.finishWrite();
	if( buffer.ring() )
		return;
	// Данные кадра (инстансы, патчи, свет) — из участка кольца в свой буфер: вид на него постоянный
	barrier( buffer.handle(), D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_UNDEFINED, true );
	flushBarriers();
	m_commandList->CopyBufferRegion( buffer.handle(), 0, m_constantRing.handle(), m_writeOffset, m_writeBytes );
	barrier( buffer.handle(), steadyAccess( buffer.desc().usage ) );
}

// ---------------------------------------------------------------------------------------------------------------------
// Привязка и вызовы (веха M4)
// ---------------------------------------------------------------------------------------------------------------------

bool DMD3D::setConstantBuffer( SRVType, uint16_t, const Buffer& buffer )
{
	// Участок этого кадра; без записи в кадре привязывать нечего
	if( buffer.ring() && buffer.ringBytes() == 0 )
		return false;
	notImplemented( "setConstantBuffer" );
	return true;
}

void DMD3D::setConstantBufferAllStages( uint16_t slot, const Buffer& buffer )
{
	setConstantBuffer( SRVType::vs, slot, buffer );
}

void DMD3D::setSRV( SRVType, uint16_t, const ShaderView& )
{
	notImplemented( "setSRV" );
}

void DMD3D::setUAV( uint16_t, const StorageView& )
{
	notImplemented( "setUAV" );
}

void DMD3D::clearStorageView( const StorageView& view )
{
	if( !view.valid() )
		return;
	barrier( view.resource(), D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS );
	flushBarriers();
	const UINT zeros[4] = {};
	m_commandList->ClearUnorderedAccessViewUint( view.descriptor().gpu, view.clearDescriptor().cpu, view.resource(), zeros, 0, nullptr );
}

void DMD3D::setVertexBuffers( uint32_t, const Buffer* const[], const uint32_t[], const uint32_t[] )
{
	notImplemented( "setVertexBuffers" );
}

void DMD3D::setVertexBuffer( const Buffer& buffer, uint32_t stride, uint32_t offset )
{
	const Buffer* buffers[] = { &buffer };
	setVertexBuffers( 1, buffers, &stride, &offset );
}

void DMD3D::setIndexBuffer( const Buffer&, DXGI_FORMAT, uint32_t )
{
	notImplemented( "setIndexBuffer" );
}

void DMD3D::unbindGeometry()
{
}

void DMD3D::draw( uint32_t, uint32_t )
{
	notImplemented( "draw" );
}

void DMD3D::drawIndexed( uint32_t, uint32_t, int32_t )
{
	notImplemented( "drawIndexed" );
}

void DMD3D::drawIndexedInstanced( uint32_t, uint32_t, uint32_t, int32_t, uint32_t )
{
	notImplemented( "drawIndexedInstanced" );
}

void DMD3D::drawIndexedInstancedIndirect( const Buffer&, uint32_t )
{
	notImplemented( "drawIndexedInstancedIndirect" );
}

void DMD3D::dispatch( uint32_t, uint32_t, uint32_t )
{
	notImplemented( "dispatch" );
}

bool DMD3D::createScreenshot()
{
	const std::wstring fileName = L"screenshot" + std::to_wstring( m_screenshotCounter++ ) + L".jpg";
	return saveScreenshot( fileName );
}

bool DMD3D::saveScreenshot( const std::wstring& )
{
	// Веха M5: ScreenGrab12
	notImplemented( "saveScreenshot" );
	return false;
}
