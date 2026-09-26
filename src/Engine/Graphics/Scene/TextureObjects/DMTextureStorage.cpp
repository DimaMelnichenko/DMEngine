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

bool DMTextureStorage::createPlaceholder()
{
	if( exists( placeholderId ) )
		return true;

	// Пурпурно-чёрная шахматка: на экране сразу видно, где не хватает текстуры
	const size_t size = 64;
	const size_t cell = 8;
	const uint32_t magenta = 0xFFFF00FF; // R8G8B8A8 в памяти: R, G, B, A
	const uint32_t black = 0xFF000000;

	ScratchImage image;
	if( FAILED( image.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1 ) ) )
		return false;

	const Image* pixels = image.GetImage( 0, 0, 0 );
	for( size_t y = 0; y < size; ++y )
	{
		uint32_t* row = reinterpret_cast<uint32_t*>( pixels->pixels + y * pixels->rowPitch );
		for( size_t x = 0; x < size; ++x )
			row[x] = ( ( x / cell + y / cell ) % 2 ) ? black : magenta;
	}

	std::unique_ptr<DDSTexture> texture( new DDSTexture( placeholderId, "placeholder", std::move( image ) ) );
	if( !texture->createSRV() )
		return false;

	return insertResource( std::move( texture ) );
}

bool DMTextureStorage::createSolid( uint32_t id, const std::string& name, uint32_t color )
{
	if( exists( id ) )
		return true;

	ScratchImage image;
	if( FAILED( image.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1 ) ) )
		return false;

	*reinterpret_cast<uint32_t*>( image.GetImage( 0, 0, 0 )->pixels ) = color;

	std::unique_ptr<DDSTexture> texture( new DDSTexture( id, name, std::move( image ) ) );
	if( !texture->createSRV() )
		return false;

	return insertResource( std::move( texture ) );
}

bool DMTextureStorage::createDefaults()
{
	// R8G8B8A8 в памяти: R, G, B, A. Плоская нормаль (0.5, 0.5, 1) в касательном пространстве
	return createSolid( whiteId, "default_white", 0xFFFFFFFF ) &&
		   createSolid( flatNormalId, "default_normal", 0xFFFF8080 );
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

	// Цветовое пространство задаёт колонка Textures.sRGB, а не метаданные файла: байты те же, меняется только
	// толкование формата (цвет — sRGB, данные вроде нормалей и масок — линейно). До генерации мипов,
	// чтобы они фильтровались в линейном пространстве
	const DXGI_FORMAT fileFormat = baseImage.GetMetadata().format;
	baseImage.OverrideFormat( sRGB ? MakeSRGB( fileFormat ) : MakeLinear( fileFormat ) );

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

	texture.reset( new DDSTexture( id, name, std::move( baseImage ) ) );

	DXGI_FORMAT format = baseImage.GetMetadata().format;

	if( !texture->createSRV() )
		return false;

	insertResource( std::move( texture ) );

	return true;
}

}