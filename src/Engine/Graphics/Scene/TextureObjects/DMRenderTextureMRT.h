#pragma once
#include "DMRenderTexture.h"

class DMRenderTextureMRT : public DMRenderTexture
{
public:
	DMRenderTextureMRT( DMD3D* );
	~DMRenderTextureMRT();

	bool Initialize( DXGI_FORMAT _format, unsigned int textureWidth, unsigned int textureHeight );
	void SetRenderTarget( bool only_depth );
	

	bool addRenderTarget( DXGI_FORMAT format );
	ID3D11ShaderResourceView* GetShaderResourceView( int index = 0 );	

private:
	ID3D11ShaderResourceView* GetShaderResourceView();

private:
	std::vector<com_unique_ptr<ID3D11Texture2D>> m_renderTargetTextures;
	std::vector<com_unique_ptr<ID3D11RenderTargetView>> m_renderTargetViews;
	std::vector<com_unique_ptr<ID3D11ShaderResourceView>> m_shaderResourceViews;
};

