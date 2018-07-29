#include "DMTextureStorage.h"

namespace GS
{

DMTextureStorage::DMTextureStorage( const std::string& path ) : DMResourceStorage( path )
{

}

DMTextureStorage::~DMTextureStorage()
{

}

bool DMTextureStorage::load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap )
{
	if( exists( id ) || exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + file;

	ScratchImage baseImage;
	if( !m_textureLoader.loadFromFile( fullPath.data(), baseImage ) )
		return false;

	std::unique_ptr<DMTexture> texture;

	
	HRESULT hr;
	if( generateMipMap )
	{
		ScratchImage mipmapImage;
		hr = GenerateMipMaps( baseImage.GetImages(), baseImage.GetImageCount(),
							  baseImage.GetMetadata(), TEX_FILTER_DEFAULT, 0, mipmapImage );

		if( SUCCEEDED( hr ) )
		{	
			std::swap( mipmapImage, baseImage );
		}
	}

	texture.reset( new DMTexture( id, name, std::move( baseImage ) ) );

	if( !texture->createSRV() )
		return false;
	
	insertResource( std::move( texture ) );

	return true;
}

}