#include "DMTextureStorage.h"
#include "DDSTexture.h"

namespace GS
{

DMTextureStorage::DMTextureStorage( const std::string& path ) : DMResourceStorage( path )
{

}

DMTextureStorage::~DMTextureStorage()
{

}

bool DMTextureStorage::load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap, bool sRGB )
{
	if( exists( id ) || exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + file;

	ScratchImage baseImage;
	if( !m_textureLoader.loadFromFile( fullPath.data(), baseImage ) )
		return false;

	std::unique_ptr<DDSTexture> texture;

	
	HRESULT hr;
	/*if( sRGB )
	{
		ScratchImage convertedImage;
		hr = Convert( baseImage.GetImages(), baseImage.GetImageCount(),
					  baseImage.GetMetadata(),
					  DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT,
					  convertedImage );
		if( SUCCEEDED( hr ) )
		{
			std::swap( convertedImage, baseImage );
		}
	}*/

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

	texture.reset( new DDSTexture( id, name, std::move( baseImage ) ) );

	DXGI_FORMAT format = baseImage.GetMetadata().format;

	if( !texture->createSRV() )
		return false;

	insertResource( std::move( texture ) );

	return true;
}

}