#pragma once

//////////////
// INCLUDES //
//////////////
#include "..\..\Common\DM3DObject.h"
#include <memory>
#include <vector>

class DMRenderTexture : public DM3DObject
{
public:
	DMRenderTexture( DMD3D* );
	~DMRenderTexture();

	bool Initialize( DXGI_FORMAT _format, unsigned int textureWidth, unsigned int textureHeight, bool use_depth_buffer = true );

	void SetRenderTarget( bool only_depth = false );
	void ClearRenderTarget( float, float, float, float );
	ID3D11ShaderResourceView* GetShaderResourceView();
	ID3D11ShaderResourceView* depthShaderResourceView( );
	
	unsigned int width();
	unsigned int height();

protected:
	bool initRTV();
	bool inintDepthStencilBuffer(  );
	bool inintDSV( ID3D11Texture2D* depthStencilBuffer );
	void setupViewport();
	bool createRenderTarget( DXGI_FORMAT format, com_unique_ptr<ID3D11Texture2D>& texture,
							 com_unique_ptr<ID3D11RenderTargetView>& rtv, com_unique_ptr<ID3D11ShaderResourceView>& srv );

private:	
	com_unique_ptr<ID3D11Texture2D> m_renderTargetTexture;
	com_unique_ptr<ID3D11RenderTargetView> m_renderTargetView;
	com_unique_ptr<ID3D11ShaderResourceView> m_shaderResourceView;	

protected:
	unsigned int m_width;
	unsigned int m_height;
	com_unique_ptr<ID3D11Texture2D> m_depthStencilBuffer;
	com_unique_ptr<ID3D11DepthStencilView> m_depthStencilView;
	com_unique_ptr<ID3D11ShaderResourceView> m_depthSRV;
	D3D11_VIEWPORT m_viewport;
	bool m_use_depth;
};

