#pragma once
#include "D3D/DMD3D.h"
#include <DirectXTex.h>

class TextureLoader
{
public:
	TextureLoader();
	~TextureLoader();

	bool loadFromFile( const char* filename, ScratchImage& image );
};

