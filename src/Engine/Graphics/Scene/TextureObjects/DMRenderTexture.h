#pragma once

//////////////
// INCLUDES //
//////////////
#include "D3D/DMD3D.h"
#include <memory>
#include <vector>

class DMRenderTexture
{
public:
	DMRenderTexture();
	DMRenderTexture( const DMRenderTexture& rtexture );
	DMRenderTexture( DMRenderTexture&& rtexture );
	~DMRenderTexture();

	DMRenderTexture& operator=( const DMRenderTexture& other );

	bool Initialize( DXGI_FORMAT _format, unsigned int textureWidth, unsigned int textureHeight, bool use_depth_buffer = true );

	void SetRenderTarget( bool only_depth = false ) const;
	void ClearRenderTarget( float, float, float, float ) const;
	ID3D11ShaderResourceView* GetShaderResourceView() const;
	ID3D11ShaderResourceView* depthShaderResourceView() const;
	
	void setWidth( float width );
	void setHeight( float height );
	unsigned int width() const;
	unsigned int height() const;

protected:
	bool initRTV();
	bool inintDepthStencilBuffer(  );
	D3D11_VIEWPORT* viewport();
	bool inintDSV( ID3D11Texture2D* depthStencilBuffer );
	void setupViewport();
	ID3D11DepthStencilView* depthStencilView();
	ID3D11Texture2D* depthStencilBuffer();
	bool createRenderTarget( DXGI_FORMAT format );

private:	
	com_shared_ptr<ID3D11Texture2D> m_renderTargetTexture;
	com_shared_ptr<ID3D11RenderTargetView> m_renderTargetView;
	com_shared_ptr<ID3D11ShaderResourceView> m_shaderResourceView;	
	com_shared_ptr<ID3D11Texture2D> m_depthStencilBuffer;
	com_shared_ptr<ID3D11DepthStencilView> m_depthStencilView;
	com_shared_ptr<ID3D11ShaderResourceView> m_depthSRV;
	D3D11_VIEWPORT m_viewport;
	bool m_use_depth;
	unsigned int m_width;
	unsigned int m_height;
};

