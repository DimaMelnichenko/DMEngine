#include "TextureLoader.h"

TextureLoader::TextureLoader()
{
}


TextureLoader::~TextureLoader()
{
}

ID3D11ShaderResourceView* TextureLoader::loadFromFile( const char* filename )
{
	HRESULT result;

	D3DX11_IMAGE_INFO imageInfo;
	// load image info
	D3DX11GetImageInfoFromFile( filename, nullptr, &imageInfo, nullptr );

	// Load the texture in.
	ID3D11ShaderResourceView* texture;
	result = D3DX11CreateShaderResourceViewFromFile( DMD3D::instance().GetDevice(), filename, NULL, NULL, &texture, NULL );
	if( FAILED( result ) )
	{
		return nullptr;
	}

	return texture;
}
