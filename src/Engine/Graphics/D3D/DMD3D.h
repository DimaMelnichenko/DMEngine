#pragma once

#include "DirectX.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Utils\utilites.h"
#include "Config\Config.h"
#include "DM3DUtils.h"
#include "GpuResources.h"
#include "DescriptorHeap.h"
#include "ConstantRing.h"
#include "RenderState.h"
#include "GpuPipeline.h"
#include "GpuPass.h"
#include "Shaders\slots.h"

namespace D3D12MA
{
class Allocator;
}

namespace Device
{

template<class ResourceType>
using CopyFunc = std::function<void( ResourceType& )>;

// Запись в буфер с BufferUsage::cpuWrite: участок upload-кольца кадра (константы — кольцо констант DMD3D::beginWrite),
// который читается в этом кадре
template<class ResourceType>
void updateResource( Buffer& buffer, CopyFunc<ResourceType> func )
{
	if( void* data = DMD3D::instance().beginWrite( buffer, sizeof( ResourceType ) ) )
		func( *static_cast<ResourceType*>( data ) );
	DMD3D::instance().endWrite();
}

template<class ResourceType>
void updateResourceData( Buffer& buffer, const ResourceType& data )
{
	if( void* slice = DMD3D::instance().beginWrite( buffer, sizeof( ResourceType ) ) )
		memcpy( slice, &data, sizeof( ResourceType ) );
	DMD3D::instance().endWrite();
}

}

// Бэкенд D3D12 за интерфейсом, которым пользуются объекты сцены, материалы и Renderer (docs/d3d12_migration.md §4):
// устройство и очереди, кадры в полёте с fence, кучи дескрипторов (bindless), кольцо констант, память (D3D12MA),
// ресурсы и виды, шейдеры и пайплайны (PSO с кэшем на диске), проходы с барьерами по объявлениям, привязка через
// таблицу привязок вызова (root-константы) и root CBV, вызовы. Вехи M1–M4 сделаны; M5 — профайлер (веха отмечена в коде)
class DMD3D
{
private:
	DMD3D();
	DMD3D( const DMD3D& ) = delete;
	DMD3D& operator=( const DMD3D& ) = delete;
	static std::unique_ptr<DMD3D> m_instance;

public:
	static constexpr uint32_t frameCount = 2;	// кадров в полёте: CPU пишет кадр N, пока GPU рисует N − 1
	// Swap chain — flip model: формат заднего буфера (в flip model только UNORM), вид на него — sRGB (тонмаппинг и GUI
	// пишут линейный цвет, перевод делает оборудование), число буферов
	static constexpr DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	static constexpr DXGI_FORMAT backBufferViewFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	static constexpr uint32_t backBufferCount = 2;
	// Буфер сцены: HDR-цвет и глубина (обратная, D32)
	static constexpr DXGI_FORMAT sceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static constexpr DXGI_FORMAT sceneDepthFormat = DXGI_FORMAT_D32_FLOAT;

	static DMD3D& instance();
	static bool exists() { return m_instance != nullptr; }
	static void destroy();
	~DMD3D();

	bool Initialize( const Config& config, HWND hwnd );
	void Shutdown();

	// --- Кадр -----------------------------------------------------------------------------------------------------------
	// Начало кадра: ожидание swap chain (waitable object) и fence кадра, который занимал эти ресурсы кадра, новый
	// командный список, новый кадр кольца констант. Команды, записанные до первого кадра (загрузка), выполняются и
	// ждутся здесь
	void beginFrame();
	// Кадр рисуется в HDR-буфер сцены (линейные значения без ограничения сверху): BeginScene объявляет проход очистки
	// и очищает его вместе с буфером глубины. Буфер создаётся при первом вызове с этим цветом очистки (optimized clear
	// value). Тонмаппинг переводит его в задний буфер (backBufferTarget + sceneColor), поверх рисуется GUI, EndScene
	// показывает кадр
	void BeginScene( float, float, float, float );
	// Проход начинается объявлением (GpuPass.h): барьеры целей, чтения и записи по объявлению (по подресурсам — мипы
	// куба, срезы каскадов), цели и область вывода ставятся, слоты вызова таблицы привязок (t0…t16, u0…u7) очищаются;
	// ресурсы прохода привязываются после beginPass. Без целей — compute-проход. Список проходов следующего кадра с
	// ресурсами — logPasses (команда passes) → log.txt
	void beginPass( const PassDesc& pass );
	void logPasses() { m_passLogRequested = true; }
	// Буфер сцены (HDR и глубина) и текущий задний буфер — цели проходов сцены, тонмаппинга и GUI
	const TargetView& sceneTarget() const { return m_sceneTarget; }
	const TargetView& sceneDepthTarget() const { return m_sceneDepth; }
	const TargetView& backBufferTarget() const { return m_backBufferTargets[m_backBufferIndex]; }
	// Размер буфера сцены (и заднего буфера), пиксели
	uint32_t sceneWidth() const { return m_screenWidth; }
	uint32_t sceneHeight() const { return m_screenHeight; }
	void clearDepth( const TargetView& target, float depth );
	void clearTarget( const TargetView& target, const float color[4] );
	// Наклонное смещение глубины растеризатора теней (Shadow Slope Bias) — параметр пайплайнов теней
	bool setShadowSlopeBias( float slopeBias );
	float shadowSlopeBias() const { return m_shadowSlopeBias; }
	// Цвет сцены для чтения в шейдере (постобработка)
	const ShaderView& sceneColor() const { return m_sceneSRV; }
	// Конец кадра: команды в очередь, Present, fence кадра
	void EndScene();
	// Ждёт, пока GPU закончит всё отправленное (fence в конце очереди) — размер окна, чтение на CPU при загрузке, выгрузка
	void waitForGpu();

	// Участок upload-кольца под size байт для buffer (BufferUsage::cpuWrite): указатель для записи до endWrite().
	// У констант buffer запоминает участок, и setConstantBuffer в этом кадре привязывает его; у остальных (инстансы,
	// патчи, свет) endWrite копирует участок в буфер — вид на него постоянный. Так работают Device::updateResource*;
	// напрямую — когда данные пишутся по месту. nullptr — участка нет (кольцо переполнено)
	void* beginWrite( Buffer& buffer, uint32_t size );
	void endWrite();
	const ConstantRing::Stats& constantRingStats() const { return m_constantRing.lastFrameStats(); }

	// Новый размер заднего буфера (WM_SIZE): дождаться GPU, отпустить все ссылки на задние буферы, ResizeBuffers,
	// затем цели заново (буфер сцены — при следующем BeginScene). Цели постобработки и проекцию камеры пересоздаёт
	// DMGraphics::resize. Тот же размер — ничего
	bool resize( uint32_t width, uint32_t height );

	// Занятая и доступная видеопамять адаптера (DXGI), байты — в «Statistic»
	struct VideoMemory
	{
		uint64_t usedBytes = 0;
		uint64_t budgetBytes = 0;
	};
	VideoMemory videoMemory() const;
	uint32_t shaderDescriptorCount() const { return m_shaderHeap.used(); }
	uint32_t barrierCount() const { return m_lastFrameBarriers; }	// барьеров за прошлый кадр

	// --- D3D12 для кода в Graphics/D3D и привязки ImGui (imgui_impl_dx12) ----------------------------------------------
	ID3D12Device10* GetDevice() const { return m_device.get(); }
	ID3D12GraphicsCommandList7* commandList() const { return m_commandList.get(); }
	ID3D12CommandQueue* directQueue() const { return m_directQueue.get(); }
	ID3D12DescriptorHeap* shaderVisibleHeap() const { return m_shaderHeap.handle(); }
	// Дескриптор из общей shader-visible кучи (ImGui — под шрифт и картинки); невалидный — куча полна
	Descriptor allocateShaderDescriptor();
	void freeShaderDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE cpu );
	// Копия всех подресурсов текстуры на CPU (TextureImages::captureTexture): раскладка подресурсов в bytes — как у
	// GetCopyableFootprints, порядок — срез за срезом, внутри среза мипы. Ждёт GPU
	struct SubresourceCopy
	{
		uint64_t offset = 0;	// байты от начала bytes
		uint32_t rowPitch = 0;	// байты между строками в bytes (выравнены на 256)
		uint32_t rows = 0;		// строк на слой глубины
		uint64_t rowBytes = 0;	// значащих байтов в строке
	};
	bool captureTexture( const Texture& texture, std::vector<SubresourceCopy>& copies, std::vector<uint8_t>& bytes );

	// --- Состояния и пайплайны (GpuPipeline.h) ------------------------------------------------------------------------
	// Состояния — часть пайплайна: setState только запоминает их (renderState), в контекст они попадают вместе с
	// шейдерами при setPipeline — DMShader::setPass после них. Порядок кода объектов: состояние → setPass → рисование
	void setState( RasterState state );
	void setState( DepthState state );
	void setState( BlendState state );
	void setRenderState( const RenderState& state );
	const RenderState& renderState() const { return m_renderState; }
	// Пайплайн по описанию из кэша (создаётся, если его ещё нет; после markPipelinesWarm — с записью в лог)
	const Pipeline& pipeline( const PipelineDesc& desc );
	// Ставит в командный список объект состояния пайплайна и топологию (root signature одна, стоит с начала списка)
	void setPipeline( const Pipeline& pipeline );
	// Цели текущего прохода (beginPass) — для описания пайплайна вызова (DMShader::setPass)
	const TargetFormats& passFormats() const { return m_passFormats; }
	// Цели проходов движка для прогрева пайплайнов: буфер сцены (HDR + глубина), только глубина (prepass, каскады
	// теней — тот же формат), задний буфер (тонмаппинг, GUI)
	static TargetFormats sceneFormats();
	static TargetFormats depthOnlyFormats();
	static TargetFormats backBufferFormats();
	// Compute-пайплайн стадии — собрать заранее (DMComputeShader::Initialize)
	void warmComputePipeline( const ShaderStage& stage ) { computePipeline( stage ); }
	// Конец загрузки уровня: набор пайплайнов собран, новые в кадре — «ленивые» (сборка PSO в кадре — фриз)
	void markPipelinesWarm() { m_pipelinesWarm = true; }
	uint32_t pipelineCount() const { return static_cast<uint32_t>( m_pipelines.size() ); }
	uint32_t lazyPipelineCount() const { return m_lazyPipelines; }

	// --- Ресурсы (GpuResources.h): создание по описаниям, память — D3D12MA ---------------------------------------------
	// Начальные данные копируются через upload-буфер в командном списке (до первого кадра — списке загрузки)
	bool createBuffer( const BufferDesc& desc, const void* initialData, Buffer& buffer );
	// Константы, которые пишут каждый кадр (BufferUsage::constant | cpuWrite) — участок кольца констант. Константы,
	// которые меняются редко, а читаются каждый кадр, — createBuffer с BufferUsage::constant и updateBuffer
	bool createShaderConstantBuffer( size_t byteSize, Buffer& buffer );
	bool createVertexBuffer( Buffer& buffer, const void* data, size_t sizeInBytes );
	bool createIndexBuffer( Buffer& buffer, const void* data, size_t sizeInBytes );
	bool createShaderView( const Buffer& buffer, const BufferViewDesc& desc, ShaderView& view );
	bool createStorageView( const Buffer& buffer, const BufferViewDesc& desc, StorageView& view );
	// initial — данные подресурсов (arraySize × mipCount, по срезам, внутри среза по мипам) или nullptr
	bool createTexture( const TextureDesc& desc, const TextureData* initial, Texture& texture );
	bool createShaderView( const Texture& texture, const TextureViewDesc& desc, ShaderView& view );
	// Цель цвета или глубины — по формату (desc.format либо формат текстуры), один мип firstMip
	bool createTargetView( const Texture& texture, const TextureViewDesc& desc, TargetView& view );
	bool createStorageView( const Texture& texture, const TextureViewDesc& desc, StorageView& view );
	// Стадия — байткод как есть; раскладка — элементы без байткода
	bool createShaderStage( SRVType type, const void* bytecode, size_t size, ShaderStage& stage );
	bool createInputLayout( const std::vector<VertexElement>& elements, const void* vsBytecode, size_t size, InputLayout& layout );

	// --- Обновление и копирование ----------------------------------------------------------------------------------------
	void updateBuffer( Buffer& buffer, const void* data, size_t size );	// запись целиком в буфер без cpuWrite: upload + копия
	void copyBuffer( Buffer& destination, const Buffer& source );
	// Чтение копии BufferUsage::readback без ожидания: false — GPU ещё пишет её (fence копии не пройден) или ошибка
	bool readBuffer( const Buffer& readback, void* data, size_t size );

	// --- Привязка ---------------------------------------------------------------------------------------------------------
	// Таблица привязок вызова (Shaders/bindless.sh): setSRV / setUAV пишут индекс дескриптора вида в слот, draw* /
	// dispatch ставят таблицу root-константами. Стадия у setSRV не важна — таблица одна на вызов; слоты сцены
	// (t100…t106) живут до следующего кадра, слоты вызова очищаются в beginPass. Вид ресурса, который пишет текущий
	// проход, на входе — барьер в состояние чтения (проход над ним закончен)
	bool setConstantBuffer( SRVType type, uint16_t slot, const Buffer& buffer );	// root CBV b0…b7
	void setConstantBufferAllStages( uint16_t slot, const Buffer& buffer );
	void setSRV( SRVType type, uint16_t slot, const ShaderView& view );
	void setUAV( uint16_t slot, const StorageView& view );		// compute
	void clearStorageView( const StorageView& view );	// нули (uint) в буфер по UAV
	void setVertexBuffers( uint32_t count, const Buffer* const buffers[], const uint32_t strides[], const uint32_t offsets[] );
	void setVertexBuffer( const Buffer& buffer, uint32_t stride, uint32_t offset = 0 );
	void setIndexBuffer( const Buffer& buffer, DXGI_FORMAT format, uint32_t offset = 0 );
	void unbindGeometry();										// без буферов вершин и индексов (вершины по SV_VertexID)
	// Осталось от D3D11, в D3D12 смысла не имеет (стадии и топология — в пайплайне, входы — таблица привязок вызова):
	// ничего не делает, убирается на вехе M5 вместе с вызовами
	void unbindSRV( SRVType, uint16_t, uint16_t = 1 ) {}
	void unbindUAVs( uint16_t, uint16_t ) {}
	// Compute-шейдер — свой пайплайн (root signature общая): DMComputeShader перед dispatch; графические стадии —
	// в setPipeline, здесь пропускаются
	void setShaderStage( SRVType type, const ShaderStage* stage );
	void unbindShaders() {}
	void setInputLayout( const InputLayout* ) {}
	void setTopology( D3D_PRIMITIVE_TOPOLOGY ) {}
	void drawAuto() {}

	// --- Вызовы -----------------------------------------------------------------------------------------------------------
	void draw( uint32_t vertexCount, uint32_t startVertex );
	void drawIndexed( uint32_t indexCount, uint32_t startIndex, int32_t baseVertex );
	void drawIndexedInstanced( uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance = 0 );
	// ExecuteIndirect с сигнатурой из одной DRAW_INDEXED — ровно то, что делал DrawIndexedInstancedIndirect D3D11
	void drawIndexedInstancedIndirect( const Buffer& args, uint32_t argsOffset );
	void dispatch( uint32_t x, uint32_t y, uint32_t z );

	// Снимки заднего буфера (PNG или JPG по расширению) — вызывать до EndScene: после Present содержимое буфера не определено
	bool createScreenshot();
	bool saveScreenshot( const std::wstring& path );

private:
	// Куча дескриптора для отложенного освобождения
	enum class HeapKind : uint8_t { shader, staging, rtv, dsv };

	struct FrameResources
	{
		com_unique_ptr<ID3D12CommandAllocator> allocator;
		uint64_t fenceValue = 0;								// GPU закончил кадр, когда fence ≥ этого значения
		std::vector<IUnknown*> deferredReleases;				// отпускаются, когда GPU закончил кадр
		std::vector<std::pair<HeapKind, Descriptor>> deferredDescriptors;
	};

	// Состояние подресурса для enhanced barriers: у буферов только sync и access, у текстур ещё layout. Новый ресурс —
	// SYNC_NONE / NO_ACCESS и начальный layout. uavWritten — после dispatch в него писали через UAV: следующая запись
	// через UAV ждёт барьер UAV → UAV (иначе две записи не упорядочены)
	struct SubresourceState
	{
		D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
		D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
		D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_NO_ACCESS;
		bool uavWritten = false;
	};
	struct ResourceState
	{
		bool texture = false;
		uint32_t mipCount = 1;
		uint32_t arraySize = 1;
		std::vector<SubresourceState> subresources;	// mipCount × arraySize у текстур, одно у буферов
	};

	bool selectAdapter();
	bool createDevice( const Config& config );
	bool createQueuesAndFrames();
	bool createDescriptorHeaps();
	bool createRootSignature();
	bool createCommandSignature();
	bool createSwapChain( HWND hwnd, bool fullscreen );
	bool createBackBufferTargets();
	void releaseBackBufferTargets();
	bool createSceneTargets( const float clearColor[4] );
	void releaseSceneTargets();
	// Объект состояния пайплайна из описания: из библиотеки на диске или собрать (и положить в библиотеку)
	bool createPipelineObject( Pipeline& pipeline );
	ID3D12PipelineState* computePipeline( const ShaderStage& stage );
	bool loadPipelineLibrary();
	void savePipelineLibrary();
	// Имя пайплайна в библиотеке — хэш содержимого описания (байткод стадий, раскладка, состояния, форматы)
	static std::wstring pipelineName( const PipelineDesc& desc );
	// Командный список: открыть на аллокаторе кадра (root signature и кучи — сразу) / закрыть и отправить в очередь
	void openCommandList( FrameResources& frame );
	void ensureRecording();	// список закрыт между кадрами — открыть (ресурсы создаются и вне кадра)
	void submitCommandList();
	// Сигнал fence в конце очереди — возвращает его значение; ожидание значения на CPU
	uint64_t signalFence();
	void waitForFence( uint64_t value );
	void waitForNextFrame();
	// Отпустить то, что кадр отложил (GPU его закончил)
	void processDeferred( FrameResources& frame );
	void deferRelease( IUnknown* object );
	void deferFreeDescriptor( HeapKind heap, const Descriptor& descriptor );
	// Барьер enhanced barriers к состоянию (access и, у текстур, layout) подресурсов range (nullptr — всех) с учётом
	// текущего; force — и при том же состоянии (запись после записи: копия за копией)
	void barrier( ID3D12Resource* resource, D3D12_BARRIER_ACCESS access, D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_UNDEFINED,
				  const SubresourceRange* range = nullptr, bool force = false );
	void flushBarriers();
	static D3D12_BARRIER_SYNC syncFor( D3D12_BARRIER_ACCESS access );
	static D3D12_BARRIER_ACCESS steadyAccess( uint32_t bufferUsage );
	// Root-аргументы (таблица привязок и CBV), изменившиеся с прошлого вызова, — в командный список перед вызовом
	void flushGraphicsRoot();
	void flushComputeRoot();
	void setBinding( uint32_t index, uint32_t descriptorIndex );
	// Ресурс из аллокатора; desc — буфера или текстуры
	bool createResource( const D3D12_RESOURCE_DESC1& desc, D3D12_HEAP_TYPE heap, D3D12_BARRIER_LAYOUT initialLayout,
						 const D3D12_CLEAR_VALUE* clearValue, ID3D12Resource** resource, D3D12MA::Allocation** allocation );
	bool createTextureInternal( const TextureDesc& desc, const TextureData* initial, Texture& texture, const float* clearColor,
								const wchar_t* name );
	// Данные — в буфер: через участок кольца (в кадре, небольшие) или отдельный upload-буфер с отложенным отпуском
	bool uploadToBuffer( ID3D12Resource* destination, uint64_t destinationOffset, const void* data, uint64_t size );
	bool uploadToTexture( ID3D12Resource* destination, const TextureDesc& desc, const TextureData* initial );
	bool createStaging( uint64_t bytes, D3D12_HEAP_TYPE heap, ID3D12Resource** resource, D3D12MA::Allocation** allocation, void** mapped );
	// Задний буфер на CPU: байты R8G8B8A8 (sRGB) с шагом строки rowPitch. Ждёт GPU; задний буфер остаётся целью
	bool captureBackBuffer( std::vector<uint8_t>& bytes, uint32_t& rowPitch );
	// Потеря устройства: причина и последние выполненные команды (DRED) — в лог
	void logDeviceRemoved( HRESULT reason );
	static void messageCallback( D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
								 LPCSTR description, void* context );
	// Метод ещё не реализован (веха M5): одна строка в лог на имя
	void notImplemented( const char* method );

	friend void gpuFreeShaderDescriptor( const Descriptor& descriptor );
	friend void gpuFreeStagingDescriptor( const Descriptor& descriptor );
	friend void gpuFreeTargetDescriptor( const Descriptor& descriptor, bool depth );
	friend void gpuReleaseResource( ID3D12Resource* resource, D3D12MA::Allocation* allocation );

private:
	bool m_vsync_enabled = false;
	int m_videoCardMemory = 0;
	char m_videoCardDescription[128] = {};
	uint32_t m_screenWidth = 0;
	uint32_t m_screenHeight = 0;
	uint32_t m_numerator = 0;
	uint32_t m_denominator = 1;
	HWND m_hWnd = nullptr;
	uint16_t m_screenshotCounter = 0;

	com_unique_ptr<IDXGIFactory4> m_factory;
	com_unique_ptr<IDXGIAdapter1> m_adapter;			// видеокарта устройства: дискретная, если их две
	com_unique_ptr<ID3D12Device10> m_device;
	D3D12MA::Allocator* m_allocator = nullptr;
	com_unique_ptr<ID3D12CommandQueue> m_directQueue;
	com_unique_ptr<ID3D12CommandQueue> m_computeQueue;	// async compute — после переезда
	com_unique_ptr<ID3D12CommandQueue> m_copyQueue;		// подгрузка во время игры — после переезда
	com_unique_ptr<ID3D12GraphicsCommandList7> m_commandList;
	bool m_recording = false;							// командный список открыт
	bool m_frameStarted = false;						// был первый beginFrame: до него команды — список загрузки
	com_unique_ptr<ID3D12Fence> m_fence;
	uint64_t m_fenceValue = 0;
	HANDLE m_fenceEvent = nullptr;
	FrameResources m_frames[frameCount];
	uint32_t m_frameIndex = 0;

	// Кучи дескрипторов: общая shader-visible CBV/SRV/UAV (дескриптор 0 — пустой SRV: непривязанный слот читает его),
	// её CPU-копия под очистки UAV, RTV и DSV
	DescriptorHeap m_shaderHeap;
	DescriptorHeap m_stagingHeap;
	DescriptorHeap m_rtvHeap;
	DescriptorHeap m_dsvHeap;
	Descriptor m_nullDescriptor;

	static constexpr uint32_t constantRingBytes = 8 * 1024 * 1024;	// на все кадры в полёте; кадр сцены — сотни участков по 256 байт
	ConstantRing m_constantRing;
	Buffer* m_writingBuffer = nullptr;
	uint32_t m_writeOffset = 0;
	uint32_t m_writeBytes = 0;

	// Состояния ресурсов для барьеров; барьеры копятся до flushBarriers — одной пачкой перед командой
	std::unordered_map<ID3D12Resource*, ResourceState> m_states;
	std::vector<D3D12_BUFFER_BARRIER> m_pendingBufferBarriers;
	std::vector<D3D12_TEXTURE_BARRIER> m_pendingTextureBarriers;
	std::vector<ID3D12Resource*> m_dispatchWrites;		// UAV, привязанные к следующему dispatch: после него — uavWritten
	uint32_t m_frameBarriers = 0;
	uint32_t m_lastFrameBarriers = 0;

	// Root-аргументы вызова: таблица привязок (индексы дескрипторов по слотам) и адреса root CBV; ставятся перед
	// вызовом, если менялись (у графики и compute — свои наборы)
	uint32_t m_bindings[DM_BINDING_COUNT] = {};
	D3D12_GPU_VIRTUAL_ADDRESS m_rootCBV[SLOT_CB_COUNT] = {};
	bool m_bindingsDirtyGraphics = true;
	bool m_bindingsDirtyCompute = true;
	uint32_t m_cbvDirtyGraphics = 0;	// бит на слот
	uint32_t m_cbvDirtyCompute = 0;
	bool m_graphicsPipelineValid = false;
	bool m_computePipelineValid = false;

	com_unique_ptr<IDXGISwapChain3> m_swapChain;
	UINT m_swapChainFlags = 0;
	bool m_allowTearing = false;
	HANDLE m_frameLatencyWaitable = nullptr;
	com_unique_ptr<ID3D12Resource> m_backBuffers[backBufferCount];
	TargetView m_backBufferTargets[backBufferCount];
	uint32_t m_backBufferIndex = 0;

	// Буфер сцены: HDR-цвет, глубина, вид цвета для постобработки — при первом BeginScene и после resize
	Texture m_sceneTexture;
	Texture m_sceneDepthTexture;
	TargetView m_sceneTarget;
	TargetView m_sceneDepth;
	ShaderView m_sceneSRV;
	float m_sceneClearColor[4] = {};
	float m_shadowSlopeBias = 0.0f;

	RenderState m_renderState;
	// Root signature одна на графику и compute: root-константы таблицы привязок b8, root CBV b0…b7, статические
	// сэмплеры s0…s8, флаг прямой индексации кучи (ResourceDescriptorHeap)
	com_unique_ptr<ID3D12RootSignature> m_rootSignature;
	com_unique_ptr<ID3D12CommandSignature> m_drawIndexedSignature;	// ExecuteIndirect: одна DRAW_INDEXED
	std::unordered_map<uint64_t, Pipeline> m_pipelines;
	std::unordered_map<uint64_t, com_unique_ptr<ID3D12PipelineState>> m_computePipelines;	// по хэшу байткода
	uint32_t m_lazyPipelines = 0;
	bool m_pipelinesWarm = false;
	// Кэш PSO на диске (cache/pipelines.bin): библиотека держит указатель на свои данные — они живут с ней
	com_unique_ptr<ID3D12PipelineLibrary1> m_pipelineLibrary;
	std::vector<uint8_t> m_pipelineLibraryData;
	bool m_pipelineLibraryDirty = false;

	// Цели текущего прохода — в описание пайплайна вызова
	TargetFormats m_passFormats;
	// Список проходов кадра для logPasses: пишется один кадр после запроса
	bool m_passLogRequested = false;
	bool m_recordingPasses = false;
	std::vector<std::string> m_passRecords;

	// Debug-слой: сообщения через callback → log.txt (одинаковые — первые три раза)
	com_unique_ptr<ID3D12InfoQueue1> m_infoQueue;
	DWORD m_messageCookie = 0;
	std::unordered_map<std::string, uint32_t> m_debugMessageCounts;	// id (+ место в шейдере у GBV) → повторов
	std::unordered_set<std::string> m_notImplemented;
};

// Запоминает состояния растеризатора, глубины и блендинга и восстанавливает их в деструкторе.
// Состояния, переданные в конструктор, действуют до конца области видимости:
// ScopedRenderState state( DepthState::disabled, RasterState::frontCulling );
class ScopedRenderState
{
public:
	template<typename... States>
	explicit ScopedRenderState( States... states ) : m_previous( DMD3D::instance().renderState() )
	{
		( DMD3D::instance().setState( states ), ... );
	}
	~ScopedRenderState()
	{
		DMD3D::instance().setRenderState( m_previous );
	}
	ScopedRenderState( const ScopedRenderState& ) = delete;
	ScopedRenderState& operator=( const ScopedRenderState& ) = delete;
	// Состояния до этой области видимости — например, каркасный режим кадра
	const RenderState& previous() const { return m_previous; }

private:
	RenderState m_previous;
};
