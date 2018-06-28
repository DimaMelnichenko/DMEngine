#include "DMTextureStorage.h"

namespace GS
{

DMTextureStorage::DMTextureStorage( const std::string& path ) : DMResourceStorage( path )
{

}

DMTextureStorage::~DMTextureStorage()
{

}

bool DMTextureStorage::load( const std::string& name )
{
	if( exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + name;

	ScratchImage baseImage;
	if( !m_textureLoader.loadFromFile( fullPath.data(), baseImage ) )
		return false;

	ScratchImage mipmapImage;
	HRESULT hr = GenerateMipMaps( baseImage.GetImages(), baseImage.GetImageCount(),
					 baseImage.GetMetadata(), TEX_FILTER_DEFAULT, 0, mipmapImage );

	std::unique_ptr<DMTexture> texture;

	if( FAILED( hr ) )
	{ 
		texture.reset( new DMTexture( nextId(), std::move( baseImage ) ) );
	}
	else
	{
		texture.reset( new DMTexture( nextId(), std::move( mipmapImage ) ) );
	}

	if( !texture->createSRV() )
		return false;
	
	insertResource( name, std::move( texture ) );

	return true;
}

}