// Устройство и очереди, кадры в полёте, swap chain и задний буфер, кучи дескрипторов, root signature; остальное —
// DMD3DPasses.cpp, DMD3DPipelines.cpp, DMD3DResources.cpp, DMD3DCommands.cpp
#include "DMD3D.h"
#include "Shaders\slots.h"
#include "Utils\utilites.h"
#include "Logger\Logger.h"
#include "DMSamplerState.h"
#include <D3D12MemAlloc.h>
#include <algorithm>
#include <cstring>
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

// Root-параметр root-констант вызова (b9): после таблицы привязок [0] и root CBV [1…SLOT_CB_COUNT]
constexpr UINT drawConstantsParameter = 1 + SLOT_CB_COUNT;

}

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
	shutdown();
}

bool DMD3D::initialize( const Settings& settings, HWND hwnd )
{
	m_backBufferWidth = settings.backBufferWidth;
	m_backBufferHeight = settings.backBufferHeight;
	m_hWnd = hwnd;
	m_vsync = settings.vsync;

	if( !selectAdapter() || !createDevice( settings.gpuValidation ) || !createQueuesAndFrames() || !createDescriptorHeaps() ||
		!createSwapChain( hwnd, settings.fullscreen ) || !createBackBufferTargets() || !createRootSignature() || !createCommandSignature() )
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
	char description[128] = {};
	size_t stringLength = 0;
	if( wcstombs_s( &stringLength, description, sizeof( description ), adapterDesc.Description, _TRUNCATE ) != 0 )
		return false;
	LOG( std::string( "Video adapter: " ) + description + ", dedicated memory " + std::to_string( adapterDesc.DedicatedVideoMemory / 1024 / 1024 ) + " MB" );
	return true;
}

bool DMD3D::createDevice( bool gpuValidation )
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
			debug1->SetEnableGPUBasedValidation( gpuValidation );
		}
		LOG( std::string( "D3D12 debug layer is enabled" ) + ( gpuValidation ? " with GPU-based validation" : "" ) +
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
	(void)gpuValidation;
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
	// Пайплайна нет в библиотеке на диске — штатно: он собирается и кладётся туда (createPipelineObject). Список из одних
	// барьеров — подсказка драйверу: так заканчивается список загрузки, когда после копий шли только барьеры
	if( id == D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND || id == D3D12_MESSAGE_ID_NON_OPTIMAL_BARRIER_ONLY_EXECUTE_COMMAND_LISTS )
		return;

	// Одно и то же сообщение обычно повторяется каждый кадр: пишется только первые maxRepeats раз. У ошибок GPU-based
	// validation один id на все шейдеры — ключ ещё и место в шейдере («Shader Code: файл(строка»)
	const uint32_t maxRepeats = 3;
	DMD3D* self = static_cast<DMD3D*>( context );
	std::string key = std::to_string( static_cast<int>( id ) );
	if( const char* code = description ? strstr( description, "Shader Code: " ) : nullptr )
	{
		const char* end = strchr( code, ',' );
		key += end ? std::string( code, end ) : code;
	}
	const uint32_t repeats = ++self->m_debugMessageCounts[key];
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
		// RTV: грани cubemap неба по мипам, уровни bloom, кадры запекания импостеров (освобождаются после GPU) — с запасом
		!m_rtvHeap.create( m_device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1024, false, L"RTV descriptors" ) ||
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

	// Пулы временных дескрипторов кадров в полёте: SRV на участки кольца (endWrite), заново с каждого кадра
	for( std::vector<Descriptor>& pool : m_transientDescriptors )
	{
		pool.resize( transientDescriptorCount );
		for( Descriptor& descriptor : pool )
		{
			descriptor = m_shaderHeap.allocate();
			if( !descriptor.valid() )
				return false;
		}
	}
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

bool DMD3D::createRootSignature()
{
	// [0] — root-константы таблицы привязок вызова (b8, DM_BINDING_COUNT DWORD: индексы дескрипторов по слотам —
	// Shaders/bindless.sh); [1…SLOT_CB_COUNT] — root CBV b0…b7 (кольцо констант и буферы проходов); [1 + SLOT_CB_COUNT] —
	// root-константы вызова b9 (их пишет команда ExecuteIndirect — начало списка инстансов); сэмплеры статические
	DMSamplerState samplers;
	samplers.initialize();
	D3D12_ROOT_PARAMETER1 parameters[2 + SLOT_CB_COUNT] = {};
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
	parameters[drawConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[drawConstantsParameter].Constants.ShaderRegister = SLOT_CB_DRAW;
	parameters[drawConstantsParameter].Constants.Num32BitValues = DM_DRAW_CONSTANT_COUNT;
	parameters[drawConstantsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
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
	// Другая root signature — другие пайплайны: библиотека на диске хранит PSO по имени, и старое имя с новой сигнатурой
	// она не приняла бы (предупреждение слоя «desc does not match»)
	m_rootSignatureHash = 14695981039346656037ull;
	for( size_t i = 0; i < blob->GetBufferSize(); ++i )
	{
		m_rootSignatureHash ^= static_cast<const uint8_t*>( blob->GetBufferPointer() )[i];
		m_rootSignatureHash *= 1099511628211ull;
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

bool DMD3D::createCommandSignature()
{
	// Команда ExecuteIndirect: root-константы вызова b9 (первое DWORD — начало списка инстансов) и DRAW_INDEXED — 24 байта;
	// сигнатура меняет root-аргументы, поэтому привязана к root signature
	D3D12_INDIRECT_ARGUMENT_DESC arguments[2] = {};
	arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	arguments[0].Constant.RootParameterIndex = drawConstantsParameter;
	arguments[0].Constant.DestOffsetIn32BitValues = 0;
	arguments[0].Constant.Num32BitValuesToSet = 1;
	arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
	D3D12_COMMAND_SIGNATURE_DESC desc = {};
	desc.ByteStride = sizeof( uint32_t ) + sizeof( D3D12_DRAW_INDEXED_ARGUMENTS );
	desc.NumArgumentDescs = 2;
	desc.pArgumentDescs = arguments;
	ID3D12CommandSignature* signature = nullptr;
	if( FAILED( m_device->CreateCommandSignature( &desc, m_rootSignature.get(), __uuidof( ID3D12CommandSignature ),
												  reinterpret_cast<void**>( &signature ) ) ) )
	{
		LOG( "CreateCommandSignature (root constant + DRAW_INDEXED) failed" );
		return false;
	}
	signature->SetName( L"DrawIndexed indirect with count" );
	m_drawIndexedCountSignature = make_com_ptr<ID3D12CommandSignature>( signature );
	return true;
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
	desc.Width = m_backBufferWidth;
	desc.Height = m_backBufferHeight;
	desc.Format = backBufferFormat;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = backBufferCount;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	desc.Flags = m_swapChainFlags;

	// Частота обновления в полноэкранном режиме — какую выберет DXGI (0 / 1)
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
	fullscreenDesc.RefreshRate.Numerator = 0;
	fullscreenDesc.RefreshRate.Denominator = 1;
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

void DMD3D::openCommandList( FrameResources& frame )
{
	if( m_recording )
		return;
	m_commandList->Reset( frame.allocator.get(), nullptr );
	// Кучи и root signature — на весь список: root-аргументы сохраняются при смене PSO и ставятся заново целиком
	ID3D12DescriptorHeap* heaps[] = { m_shaderHeap.handle() };
	m_commandList->SetDescriptorHeaps( 1, heaps );
	m_commandList->SetGraphicsRootSignature( m_rootSignature.get() );
	m_commandList->SetComputeRootSignature( m_rootSignature.get() );
	// Root-константы вызова заданы всегда: их пишут только команды ExecuteIndirect
	const uint32_t drawConstants[DM_DRAW_CONSTANT_COUNT] = {};
	m_commandList->SetGraphicsRoot32BitConstants( drawConstantsParameter, DM_DRAW_CONSTANT_COUNT, drawConstants, 0 );
	m_bindingsDirtyGraphics = m_bindingsDirtyCompute = true;
	m_cbvDirtyGraphics = m_cbvDirtyCompute = ( 1u << SLOT_CB_COUNT ) - 1;
	m_graphicsPipelineValid = m_computePipelineValid = false;
	m_recording = true;
}

void DMD3D::ensureRecording()
{
	// Ресурс создаётся между endFrame и beginFrame (WM_SIZE, загрузка по команде): список закрыт — открыть на аллокаторе
	// текущего кадра; beginFrame выполнит записанное и дождётся, прежде чем сбросить аллокатор
	if( !m_recording && m_device )
		openCommandList( m_frames[m_frameIndex] );
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
	m_inFrame = true;

	FrameResources& frame = m_frames[m_frameIndex];
	waitForFence( frame.fenceValue );
	processDeferred( frame );
	frame.allocator->Reset();
	openCommandList( frame );

	// Адреса констант прошлого кадра указывают в часть кольца, которую CPU уже переписывает: забыть их — шейдер, который
	// читает не записанный в этом кадре буфер, получит ошибку debug-слоя, а не мусор
	memset( m_rootCBV, 0, sizeof( m_rootCBV ) );
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
	// Задний буфер — цель этого кадра (у него свой учёт: PRESENT ↔ RENDER_TARGET, обратно — endFrame)
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
	m_constantRing.beginFrame( m_frameIndex );
	m_transientUsed = 0;
	m_passFormats = {};
	m_lastFrameBarriers = m_frameBarriers;
	m_frameBarriers = 0;
	m_lastFrameIndirectDraws = m_frameIndirectDraws;
	m_frameIndirectDraws = 0;
	if( m_passLogRequested )
	{
		m_passLogRequested = false;
		m_recordingPasses = true;
		m_passRecords.clear();
		m_passBarriersStart = 0;
	}
}

void DMD3D::endFrame()
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
	m_inFrame = false;

	// С vsync — по частоте монитора; без него — сразу, с разрывом кадра (tearing), если DXGI его поддерживает
	const UINT flags = !m_vsync && m_allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
	const HRESULT hr = m_swapChain->Present( m_vsync ? 1 : 0, flags );
	m_frames[m_frameIndex].fenceValue = signalFence();
	if( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET )
		logDeviceRemoved( m_device->GetDeviceRemovedReason() );

	if( m_recordingPasses )
	{
		finishPassRecord();
		m_recordingPasses = false;
		LOG( "Frame passes: " + std::to_string( m_passRecords.size() ) + ", barriers: " + std::to_string( m_frameBarriers ) +
			 ", indirect draws: " + std::to_string( m_frameIndirectDraws ) );
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
	if( width == m_backBufferWidth && height == m_backBufferHeight )
		return true;

	// GPU закончил с задними буферами → ссылок на них нет → ResizeBuffers → виды заново
	waitForGpu();
	releaseBackBufferTargets();
	const HRESULT hr = m_swapChain->ResizeBuffers( backBufferCount, width, height, backBufferFormat, m_swapChainFlags );
	if( FAILED( hr ) )
	{
		LOG( "ResizeBuffers " + std::to_string( width ) + "x" + std::to_string( height ) + " failed, HRESULT " + std::to_string( static_cast<long>( hr ) ) );
		return false;
	}
	m_backBufferWidth = width;
	m_backBufferHeight = height;
	if( !createBackBufferTargets() )
		return false;
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
	LOG( "Back buffer resized to " + std::to_string( width ) + "x" + std::to_string( height ) );
	return true;
}

void DMD3D::shutdown()
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
	m_timestampHeap.reset();
	m_drawIndexedCountSignature.reset();
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
