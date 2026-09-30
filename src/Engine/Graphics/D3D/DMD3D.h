#pragma once

/////////////
// LINKING //
/////////////
//////////////
// INCLUDES //
//////////////


#include "DirectX.h"
#include <list>
#include <memory>
#include <unordered_map>
#include "Utils\utilites.h"
#include "Config\Config.h"
#include "DM3DUtils.h"
#include "GpuResources.h"
#include "ConstantRing.h"
#include "RenderState.h"
#include "GpuPipeline.h"
#include "GpuPass.h"

namespace Device
{

template<class ResourceType>
using CopyFunc = std::function<void( ResourceType& )>;

// Запись в буфер с BufferUsage::cpuWrite: константы — в участок кольца констант кадра (DMD3D::beginConstants),
// остальные (инстансы, патчи) — Map( WRITE_DISCARD ) своего DYNAMIC-буфера
template<class ResourceType>
void updateResource( Buffer& buffer, CopyFunc<ResourceType> func, D3D11_MAP MapFlags = D3D11_MAP_WRITE_DISCARD )
{
	if( buffer.ring() )
	{
		if( void* data = DMD3D::instance().beginConstants( buffer, sizeof( ResourceType ) ) )
			func( *static_cast<ResourceType*>( data ) );
		DMD3D::instance().endConstants();
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer.handle(), 0, MapFlags, 0, &mappedResource );

	ResourceType* data = static_cast<ResourceType*>( mappedResource.pData );

	func( *data );

	DMD3D::instance().GetDeviceContext()->Unmap( buffer.handle(), 0 );
}

template<class ResourceType>
void updateResourceData( Buffer& buffer, const ResourceType& data, D3D11_MAP MapFlags = D3D11_MAP_WRITE_DISCARD )
{
	if( buffer.ring() )
	{
		if( void* slice = DMD3D::instance().beginConstants( buffer, sizeof( ResourceType ) ) )
			memcpy( slice, &data, sizeof( ResourceType ) );
		DMD3D::instance().endConstants();
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer.handle(), 0, MapFlags, 0, &mappedResource );

	memcpy( mappedResource.pData, &data, sizeof( ResourceType ) );

	DMD3D::instance().GetDeviceContext()->Unmap( buffer.handle(), 0 );
}

}

class DMD3D
{
private:
	DMD3D(  );
	DMD3D( const DMD3D& ) = delete;
	DMD3D& operator=( const DMD3D& ) = delete;

	static std::unique_ptr<DMD3D> m_instance;

public:
	static DMD3D& instance();
	static void destroy();
	~DMD3D();
	

	bool Initialize( const Config& config, HWND hwnd );
	void Shutdown( );

	// Кадр рисуется в HDR-буфер сцены (R16G16B16A16_FLOAT, линейные значения без ограничения сверху):
	// BeginScene привязывает его вместе с буфером глубины и очищает. Тонмаппинг переводит его в задний буфер
	// (setBackBufferTarget + sceneColor), поверх рисуется GUI, EndScene показывает кадр
	void BeginScene( float, float, float, float );
	// Проход начинается объявлением (GpuPass.h): цели и область вывода ставятся, транзитные слоты (t0…) и виды ресурсов,
	// в которые проход пишет, снимаются со входов всех стадий; ресурсы прохода привязываются после beginPass. Без
	// целей — compute-проход. Список проходов следующего кадра с ресурсами — logPasses (команда passes) → log.txt
	void beginPass( const PassDesc& pass );
	void logPasses() { m_passLogRequested = true; }
	// Буфер сцены (HDR и глубина) и задний буфер — цели проходов сцены и тонмаппинга
	const TargetView& sceneTarget() const { return m_sceneTarget; }
	const TargetView& sceneDepthTarget() const { return m_sceneDepth; }
	const TargetView& backBufferTarget() const { return m_backBufferTarget; }
	// Размер буфера сцены (и заднего буфера), пиксели
	uint32_t sceneWidth() const { return m_screenWidth; }
	uint32_t sceneHeight() const { return m_screenHeight; }
	void clearDepth( const TargetView& target, float depth );
	void clearTarget( const TargetView& target, const float color[4] );
	// Наклонное смещение глубины растеризатора теней (Shadow Slope Bias): пересоздаёт состояние
	bool setShadowSlopeBias( float slopeBias );
	// Цвет сцены для чтения в шейдере; при MSAA сначала сводит выборки в обычную текстуру
	const ShaderView& sceneColor();
	void EndScene( );
	// Начало кадра: ждём сигнала swap chain (frame latency waitable object), что очередь кадров короче предела, —
	// ввод и камера берутся позже, задержка меньше, чем при блокировке в Present. Без объекта ничего не делает
	void waitForNextFrame();
	// Начало кадра: ожидание swap chain (waitForNextFrame) и новый кадр кольца констант
	void beginFrame();
	// Участок кольца констант под size байт для buffer (BufferUsage::constant | cpuWrite): указатель для записи до
	// endConstants(); buffer запоминает участок, и setConstantBuffer в этом кадре привязывает его. Так работают
	// Device::updateResource*; напрямую — когда данные пишутся по месту
	void* beginConstants( Buffer& buffer, uint32_t size );
	void endConstants();
	const ConstantRing::Stats& constantRingStats() const { return m_constantRing.lastFrameStats(); }
	// Новый размер заднего буфера (WM_SIZE) по правилу D3D12: дождаться GPU, отпустить все ссылки на задние буферы,
	// ResizeBuffers, затем буфер сцены, глубина и область вывода заново. Цели постобработки и проекцию камеры
	// пересоздаёт DMGraphics::resize. Тот же размер — ничего не делает
	bool resize( uint32_t width, uint32_t height );

	// Устройство и контекст D3D11 — только для кода в Graphics/D3D и привязки ImGui (imgui_impl_dx11); объекты сцены
	// и материалы работают через методы ниже и ресурсы GpuResources.h
	ID3D11Device* GetDevice( );
	ID3D11DeviceContext* GetDeviceContext( );

	// Состояния — часть пайплайна: setState только запоминает их (renderState), в контекст они попадают вместе с
	// шейдерами при setPipeline — DMShader::setPass после них. Порядок кода объектов: состояние → setPass → рисование
	void setState( RasterState state );
	void setState( DepthState state );
	void setState( BlendState state );
	void setRenderState( const RenderState& state );
	const RenderState& renderState() const;

	// --- Пайплайны (Pipeline.h) ------------------------------------------------------------------------------------------
	// Пайплайн по описанию из кэша (создаётся, если его ещё нет; после markPipelinesWarm — с записью в лог)
	const Pipeline& pipeline( const PipelineDesc& desc );
	// Ставит в контекст всё, что в пайплайне: раскладку, стадии, состояния, топологию
	void setPipeline( const Pipeline& pipeline );
	// Конец загрузки уровня: набор пайплайнов собран, новые в кадре — «ленивые» (в D3D12 — фриз)
	void markPipelinesWarm() { m_pipelinesWarm = true; }
	uint32_t pipelineCount() const { return static_cast<uint32_t>( m_pipelines.size() ); }
	uint32_t lazyPipelineCount() const { return m_lazyPipelines; }

	// --- Ресурсы (GpuResources.h): создание по описаниям без типов D3D11 ---------------------------------------------
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
	bool createShaderStage( SRVType type, const void* bytecode, size_t size, ShaderStage& stage );
	bool createInputLayout( const std::vector<VertexElement>& elements, const void* vsBytecode, size_t size, InputLayout& layout );

	// --- Обновление и копирование ------------------------------------------------------------------------------------
	void updateBuffer( Buffer& buffer, const void* data, size_t size );	// запись целиком в буфер без cpuWrite (UpdateSubresource)
	void copyBuffer( Buffer& destination, const Buffer& source );
	// Чтение копии BufferUsage::readback без ожидания: false — GPU ещё пишет её или ошибка
	bool readBuffer( const Buffer& readback, void* data, size_t size );
	void generateMips( const ShaderView& view );		// текстура с TextureUsage::generateMips

	// --- Привязка -------------------------------------------------------------------------------------------------------
	bool setConstantBuffer( SRVType type, uint16_t slot, const Buffer& buffer );
	void setConstantBufferAllStages( uint16_t slot, const Buffer& buffer );	// VS, HS, DS, GS, PS и CS
	void setSRV( SRVType type, uint16_t slot, const ShaderView& view );
	void unbindSRV( SRVType type, uint16_t slot, uint16_t count = 1 );
	void setUAV( uint16_t slot, const StorageView& view );		// compute
	void unbindUAVs( uint16_t first, uint16_t count );
	void clearStorageView( const StorageView& view );	// нули (uint) в буфер по UAV
	void setShaderStage( SRVType type, const ShaderStage* stage );	// nullptr — стадия выключена
	void unbindShaders();										// все стадии выключены (перед ImGui)
	void setInputLayout( const InputLayout* layout );
	void setVertexBuffers( uint32_t count, const Buffer* const buffers[], const uint32_t strides[], const uint32_t offsets[] );
	void setVertexBuffer( const Buffer& buffer, uint32_t stride, uint32_t offset = 0 );
	void setIndexBuffer( const Buffer& buffer, DXGI_FORMAT format, uint32_t offset = 0 );
	void unbindGeometry();										// без буферов вершин и индексов (вершины по SV_VertexID)
	void setTopology( D3D_PRIMITIVE_TOPOLOGY topology );

	// --- Вызовы ---------------------------------------------------------------------------------------------------------
	void draw( uint32_t vertexCount, uint32_t startVertex );
	void drawIndexed( uint32_t indexCount, uint32_t startIndex, int32_t baseVertex );
	void drawIndexedInstanced( uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance = 0 );
	void drawIndexedInstancedIndirect( const Buffer& args, uint32_t argsOffset );
	void drawAuto();
	void dispatch( uint32_t x, uint32_t y, uint32_t z );

	bool createScreenshot();
	// Задний буфер в файл (PNG или JPG по расширению) — вызывать до EndScene: после Present содержимое буфера не определено
	bool saveScreenshot( const std::wstring& path );

	// Переносит в log.txt ошибки и предупреждения debug-слоя D3D11, накопленные с прошлого вызова. Без отладчика
	// их больше нигде не видно. Вызывается после каждого кадра; без debug-слоя ничего не делает
	void logDebugMessages();

private:
	bool createDeviceSwapChain( HWND, bool fullscreen );
	bool createRenderTargetView();
	bool createSceneTarget();
	bool createDepthBuffer();		// текстура и вид глубины сцены — заново при resize
	bool createDepthStates();		// состояния глубины — один раз
	// Отвязывает и отпускает задний буфер, буфер сцены и глубину: перед ResizeBuffers ссылок на задние буферы быть не должно
	void releaseSizedTargets();
	// Ждёт, пока GPU закончит всё отправленное (как ожидание fence в D3D12)
	void waitForGpu();
	bool createRasterDescs();
	bool createViewport();
	bool createBlendStates();

	bool createRasterizerState( D3D11_RASTERIZER_DESC& desc, com_unique_ptr<ID3D11RasterizerState>& state );
	bool createBlendState( D3D11_BLEND_DESC& desc, com_unique_ptr<ID3D11BlendState>& state );
	
private:
	bool m_vsync_enabled;
	int m_videoCardMemory;
	char m_videoCardDescription[128];
	com_unique_ptr<IDXGIAdapter1> m_adapter;			// видеокарта устройства: дискретная, если их две
	com_unique_ptr<IDXGISwapChain1> m_swapChain;
	com_unique_ptr<ID3D11Device> m_device;
	com_unique_ptr<ID3D11DeviceContext> m_deviceContext;
	com_unique_ptr<ID3D11DeviceContext1> m_deviceContext1;	// D3D 11.1: привязка констант со смещением
	static constexpr uint32_t constantRingBytes = 4 * 1024 * 1024;	// с запасом: кадр сцены — сотни участков по 256 байт
	ConstantRing m_constantRing;

	TargetView m_backBufferTarget;								// задний буфер
	com_unique_ptr<ID3D11Texture2D> m_sceneTexture;				// HDR-буфер сцены, с MSAA — многовыборочный
	TargetView m_sceneTarget;
	com_unique_ptr<ID3D11Texture2D> m_sceneResolved;			// только при MSAA: сведённые выборки для чтения
	ShaderView m_sceneSRV;
	com_unique_ptr<ID3D11Texture2D> m_depthStencilBuffer;
	com_unique_ptr<ID3D11DepthStencilState> m_depthStencilState;
	com_unique_ptr<ID3D11DepthStencilState> m_depthDisabledStencilState;
	com_unique_ptr<ID3D11DepthStencilState> m_depthReadOnlyStencilState;
	com_unique_ptr<ID3D11DepthStencilState> m_depthReadOnlyNearOrEqualStencilState;
	com_unique_ptr<ID3D11DepthStencilState> m_depthReadOnlyEqualStencilState;
	TargetView m_sceneDepth;

	com_unique_ptr<ID3D11RasterizerState> m_rasterState;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateFrontCulling;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateNoCulling;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateWireframe;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateSolidMirrored;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateNoCullingMirrored;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateShadowDepth;

	com_unique_ptr<ID3D11BlendState> m_alphaEnableBlendingState;
	com_unique_ptr<ID3D11BlendState> m_alphaDisableBlendingState;
	com_unique_ptr<ID3D11BlendState> m_additiveBlendingState;

	RenderState m_renderState;
	std::unordered_map<uint64_t, Pipeline> m_pipelines;
	uint32_t m_lazyPipelines = 0;
	bool m_pipelinesWarm = false;

	// Отвязывает ресурсы материалов, проходов и объектов (слоты до SLOT_TRANSIENT_COUNT) у всех стадий: только занятые
	// через setSRV (в кадре — beginPass, десятки раз) или все подряд (в конце кадра: ImGui привязывает свои сам)
	void unbindTransientResources( bool all );
	// Что привязано по стадиям и слотам (ресурсы видов): beginPass снимает виды того, во что пишет
	static int stageIndex( SRVType type );
	static constexpr int stageCount = 6;
	ID3D11Resource* m_boundSRVs[stageCount][D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
	uint16_t m_boundTransientEnd[stageCount] = {};	// за последним занятым транзитным слотом стадии
	ID3D11Resource* m_boundUAVs[D3D11_1_UAV_SLOT_COUNT] = {};
	// Ресурсы целей текущего прохода: их вид на входе (setSRV) означает, что проход над ними закончен — цели снимаются
	ID3D11Resource* m_passTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT + 1] = {};
	uint32_t m_passTargetCount = 0;
	void makeReadable( ID3D11Resource* resource );
	// Список проходов кадра для logPasses: пишется один кадр после запроса
	bool m_passLogRequested = false;
	bool m_recordingPasses = false;
	std::vector<std::string> m_passRecords;

	ID3D11RasterizerState* rasterObject( RasterState state ) const;
	ID3D11DepthStencilState* depthObject( DepthState state ) const;
	ID3D11BlendState* blendObject( BlendState state ) const;

	D3D11_VIEWPORT m_viewport;

	uint32_t m_screenWidth;
	uint32_t m_screenHeight;
	uint32_t m_numerator;
	uint32_t m_denominator;
	uint32_t m_MSAACount = 1;
	HWND m_hWnd;
	uint16_t m_screenshotCounter = 0;

	// Swap chain — flip model: формат заднего буфера (в flip model только UNORM, sRGB — у вида заднего буфера), число
	// буферов, флаги создания (те же у ResizeBuffers), tearing без vsync, waitable object начала кадра
	static constexpr DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	static constexpr uint32_t backBufferCount = 2;
	UINT m_swapChainFlags = 0;
	bool m_allowTearing = false;
	HANDLE m_frameLatencyWaitable = nullptr;

	com_unique_ptr<ID3D11InfoQueue> m_infoQueue;	// только с debug-слоем
	std::unordered_map<int, uint32_t> m_debugMessageCounts;	// сколько раз записано сообщение с этим D3D11_MESSAGE_ID
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

