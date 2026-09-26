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

enum class RasterState
{
	solid, frontCulling, noCulling, wireframe
};

enum class DepthState
{
	enabled,	// проверка и запись
	readOnly,	// только проверка: полупрозрачные
	disabled
};

enum class BlendState
{
	opaque, alpha
};

// Отсечение граней для материала: двусторонний рисуется без отсечения, каркасный режим кадра (Q) остаётся каркасом
inline RasterState materialRasterState( bool twoSided, RasterState frameState )
{
	return twoSided && frameState == RasterState::solid ? RasterState::noCulling : frameState;
}

struct RenderState
{
	RasterState raster = RasterState::solid;
	DepthState depth = DepthState::enabled;
	BlendState blend = BlendState::opaque;
};

namespace Device
{

template<class ResourceType>
using CopyFunc = std::function<void( ResourceType& )>;

template<class ResourceType>
void updateResource( com_unique_ptr<ID3D11Buffer>& buffer, CopyFunc<ResourceType> func, D3D11_MAP MapFlags = D3D11_MAP_WRITE_DISCARD )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer.get(), 0, MapFlags, 0, &mappedResource );

	ResourceType* data = static_cast<ResourceType*>( mappedResource.pData );

	func( *data );

	DMD3D::instance().GetDeviceContext()->Unmap( buffer.get(), 0 );
}

template<class ResourceType>
void updateResourceData( ID3D11Buffer* buffer, ResourceType& data, D3D11_MAP MapFlags = D3D11_MAP_WRITE_DISCARD )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer, 0, MapFlags, 0, &mappedResource );

	memcpy( mappedResource.pData, &data, sizeof( ResourceType ) );

	DMD3D::instance().GetDeviceContext()->Unmap( buffer, 0 );
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
	// Цели рендера с областью вывода по их размеру. Проход ставит свою цель сам, а не полагается на оставленную
	void setSceneTarget();		// HDR-буфер сцены и буфер глубины, область вывода на весь кадр
	void setBackBufferTarget();	// задний буфер без глубины
	void setRenderTarget( ID3D11RenderTargetView* target, uint32_t width, uint32_t height );	// без глубины
	// Отвязывает ресурсы материалов, проходов и объектов (слоты до SLOT_TRANSIENT_COUNT) у графических стадий
	void unbindTransientResources();
	// Цвет сцены для чтения в шейдере; при MSAA сначала сводит выборки в обычную текстуру
	const com_unique_ptr<ID3D11ShaderResourceView>& sceneColor();
	void EndScene( );

	ID3D11Device* GetDevice( );
	ID3D11DeviceContext* GetDeviceContext( );

	void setState( RasterState state );
	void setState( DepthState state );
	void setState( BlendState state );
	void setRenderState( const RenderState& state );
	const RenderState& renderState() const;

	bool createShaderConstantBuffer( size_t byte_size, com_unique_ptr<ID3D11Buffer> &, const D3D11_SUBRESOURCE_DATA* = nullptr );
	bool setConstantBuffer( SRVType type, uint16_t slot, com_unique_ptr<ID3D11Buffer>& );
	bool createSRV( const com_unique_ptr<ID3D11Buffer>& buffer, D3D11_SHADER_RESOURCE_VIEW_DESC& desc, com_unique_ptr<ID3D11ShaderResourceView>& srv );
	void setSRV( SRVType type, uint16_t slot, const com_unique_ptr<ID3D11ShaderResourceView>& srv );
	bool createUAV( const com_unique_ptr<ID3D11Buffer>& buffer, D3D11_UNORDERED_ACCESS_VIEW_DESC& desc, com_unique_ptr<ID3D11UnorderedAccessView>& uav );
	bool createVertexBuffer( com_unique_ptr<ID3D11Buffer> &, void* data, size_t sizeInByte );
	bool createIndexBuffer( com_unique_ptr<ID3D11Buffer> &, void* data, size_t sizeInByte );
	bool CreateBuffer( const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, com_unique_ptr<ID3D11Buffer>& );

	bool createScreenshot();

	// Переносит в log.txt ошибки и предупреждения debug-слоя D3D11, накопленные с прошлого вызова. Без отладчика
	// их больше нигде не видно. Вызывается после каждого кадра; без debug-слоя ничего не делает
	void logDebugMessages();

private:
	bool createDeviceSwapChain( HWND, bool fullscreen );
	bool createRenderTargetView();
	bool createSceneTarget();
	bool createDepthStencilBufferAndView();
	bool createRasterDescs();
	bool createViewport();
	bool createBlendStates();

	bool createRasterizerState( D3D11_RASTERIZER_DESC& desc, com_unique_ptr<ID3D11RasterizerState>& state );
	bool createBlendState( D3D11_BLEND_DESC& desc, com_unique_ptr<ID3D11BlendState>& state );
	
private:
	bool m_vsync_enabled;
	int m_videoCardMemory;
	char m_videoCardDescription[128];
	com_unique_ptr<IDXGISwapChain> m_swapChain;
	com_unique_ptr<ID3D11Device> m_device;
	com_unique_ptr<ID3D11DeviceContext> m_deviceContext;

	com_unique_ptr<ID3D11RenderTargetView> m_renderTargetView;	// задний буфер
	com_unique_ptr<ID3D11Texture2D> m_sceneTexture;				// HDR-буфер сцены, с MSAA — многовыборочный
	com_unique_ptr<ID3D11RenderTargetView> m_sceneRTV;
	com_unique_ptr<ID3D11Texture2D> m_sceneResolved;			// только при MSAA: сведённые выборки для чтения
	com_unique_ptr<ID3D11ShaderResourceView> m_sceneSRV;
	com_unique_ptr<ID3D11Texture2D> m_depthStencilBuffer;
	com_unique_ptr<ID3D11DepthStencilState> m_depthStencilState;
	com_unique_ptr<ID3D11DepthStencilState> m_depthDisabledStencilState;
	com_unique_ptr<ID3D11DepthStencilState> m_depthReadOnlyStencilState;
	com_unique_ptr<ID3D11DepthStencilView> m_depthStencilView;

	com_unique_ptr<ID3D11RasterizerState> m_rasterState;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateFrontCulling;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateNoCulling;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateWireframe;

	com_unique_ptr<ID3D11BlendState> m_alphaEnableBlendingState;
	com_unique_ptr<ID3D11BlendState> m_alphaDisableBlendingState;

	RenderState m_renderState;

	D3D11_VIEWPORT m_viewport;

	uint32_t m_screenWidth;
	uint32_t m_screenHeight;
	uint32_t m_numerator;
	uint32_t m_denominator;
	uint32_t m_MSAACount = 1;
	HWND m_hWnd;
	uint16_t m_screenshotCounter = 0;

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

