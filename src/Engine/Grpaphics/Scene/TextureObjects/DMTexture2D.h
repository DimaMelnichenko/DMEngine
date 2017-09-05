#pragma once

#include "DMObject.h"
#include <d3d11.h>
#include <d3dx11tex.h>

class DMTexture2D : public DMObject
{
public:
	DMTexture2D(  );
	~DMTexture2D();

	bool Initialize( ID3D11Device* device, ID3D11DeviceContext*, const WCHAR* filename );

private:
	ID3D11Texture2D* m_texture;
	//D3DX11CreateTextureFromResource
};

