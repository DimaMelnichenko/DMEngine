#pragma once

#include "DMRenderTexture.h"
#include "..\Camera\DMCamera.h"

class DMRenderCubeTexture :	public DMRenderTexture
{
public:
	DMRenderCubeTexture( DMD3D* );
	~DMRenderCubeTexture();

	bool Initialize( DXGI_FORMAT _format, unsigned int textureWidth, unsigned int textureHeight );

	
	bool initLoop( const D3DXVECTOR3& pos );
	bool initLoop( float x, float y, float z );
	DMCamera* next();

	ID3D11ShaderResourceView* GetShaderResourceView();

protected:
	void initCameras();
	void SetRenderTarget( bool only_depth = false );
	void ClearRenderTarget( float, float, float, float );
	bool createRenderTarget( DXGI_FORMAT format );

private:
	std::vector<std::unique_ptr<DMCamera>> m_cameras;
	com_unique_ptr<ID3D11Texture2D> m_renderTargetTexture;
	std::vector<com_unique_ptr<ID3D11RenderTargetView>> m_renderTargetViews;
	com_unique_ptr<ID3D11ShaderResourceView> m_shaderResourceView;
	size_t m_loop_index;

};

