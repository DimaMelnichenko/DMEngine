#pragma once
#include "D3D/DMD3D.h"
#include <D3DX11tex.h>

class TextureLoader
{
public:
	TextureLoader();
	~TextureLoader();

	ID3D11ShaderResourceView* loadFromFile( const WCHAR* filename );
};

