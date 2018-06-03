#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include "Utils\utilites.h"


class Texture2DArray
{
public:
	Texture2DArray( size_t count );
	~Texture2DArray();

	bool addTextures( const char* );

	ID3D11ShaderResourceView** getTextureArray();
	int count( ) const;

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_textures;
};

