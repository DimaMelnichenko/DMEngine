#pragma once

/////////////
// LINKING //
/////////////
/*
#pragma comment( lib, "dxgi.lib")
#pragma comment( lib, "d3d11.lib")
#pragma comment( lib, "d3dx11.lib")
#pragma comment( lib, "d3dx10.lib")
*/
//////////////
// INCLUDES //
//////////////


#include "DirectX.h"
#include <list>
#include <memory>
#include "Utils\utilites.h"
#include "Config\Config.h"
#include "DM3DUtils.h"

namespace Device
{

template<class ResourceType>
using CopyFunc = std::function<void( ResourceType& )>;

template<class ResourceType>
void updateResource( ID3D11Buffer* buffer, CopyFunc<ResourceType> func, D3D11_MAP MapFlags = D3D11_MAP_WRITE_DISCARD )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer, 0, MapFlags, 0, &mappedResource );

	ResourceType* data = static_cast<ResourceType*>( mappedResource.pData );

	func( *data );

	DMD3D::instance().GetDeviceContext()->Unmap( buffer, 0 );
}

template<class ResourceType>
void updateResource( ID3D11Buffer* buffer, ResourceType& data, D3D11_MAP MapFlags = D3D11_MAP_WRITE_DISCARD )
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

	void BeginScene( float, float, float, float );
	void EndScene( );

	ID3D11Device* GetDevice( );
	ID3D11DeviceContext* GetDeviceContext( );

	void GetVideoCardInfo( char*, int& );

	void TurnZBufferOn( );
	void TurnZBufferOff( );

	void currentRS( com_unique_ptr<ID3D11RasterizerState>& );
	void setRS( ID3D11RasterizerState* );
	void TurnDefaultRS( );
	void TurnBackFacesRS();
	void TurnShadowRS();
	void TurnCullingNoneRS( );

	void TurnOnWireframe( );
	void TurnOffWireframe( );

	void TurnOnAlphaBlending( );
	void TurnOffAlphaBlending( );

	ID3D11DepthStencilView* GetDepthStencilView( );
	void SetBackBufferRenderTarget( );
	void ResetViewport( );

	void TurnOnTransparancy( );
	void TurnOffTransparancy( );

	bool createShaderConstantBuffer( size_t byte_size, com_unique_ptr<ID3D11Buffer> &, const D3D11_SUBRESOURCE_DATA* = nullptr );
	bool setConstantBuffer( SRVType type, uint16_t slot, com_unique_ptr<ID3D11Buffer>& );
	bool createSRV( const com_unique_ptr<ID3D11Buffer>& buffer, D3D11_SHADER_RESOURCE_VIEW_DESC& desc, com_unique_ptr<ID3D11ShaderResourceView>& srv );
	bool createUAV( const com_unique_ptr<ID3D11Buffer>& buffer, D3D11_UNORDERED_ACCESS_VIEW_DESC& desc, com_unique_ptr<ID3D11UnorderedAccessView>& uav );
	bool createVertexBuffer( com_unique_ptr<ID3D11Buffer> &, void* data, size_t sizeInByte );
	bool createIndexBuffer( com_unique_ptr<ID3D11Buffer> &, void* data, size_t sizeInByte );
	bool CreateBuffer( const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, com_unique_ptr<ID3D11Buffer>& );

private:
	bool createDeviceSwapChain( HWND, bool fullscreen );
	bool createRenderTargetView();
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

	com_unique_ptr<ID3D11RenderTargetView> m_renderTargetView;
	com_unique_ptr<ID3D11Texture2D> m_depthStencilBuffer;
	com_unique_ptr<ID3D11DepthStencilState> m_depthStencilState;
	com_unique_ptr<ID3D11DepthStencilState> m_depthDisabledStencilState;
	com_unique_ptr<ID3D11DepthStencilView> m_depthStencilView;

	com_unique_ptr<ID3D11RasterizerState> m_rasterState;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateFrontCulling;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateShadow;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateNoCulling;
	com_unique_ptr<ID3D11RasterizerState> m_rasterStateWireframe;

	com_unique_ptr<ID3D11BlendState> m_alphaEnableBlendingState;
	com_unique_ptr<ID3D11BlendState> m_alphaDisableBlendingState;
	com_unique_ptr<ID3D11BlendState> m_blendStateTransparency;

	D3D11_VIEWPORT m_viewport;

	uint32_t m_screenWidth;
	uint32_t m_screenHeight;
	uint32_t m_numerator;
	uint32_t m_denominator;
	HWND m_hWnd;
};

