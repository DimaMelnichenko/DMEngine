#include "DMTextureStorage.h"
#include <random>
#include "ImageFile.h"
#include "ImageMips.h"
#include "Logger\Logger.h"

using namespace DirectX;

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

	auto texture = std::make_unique<DMTexture>( placeholderId, "placeholder" );
	if( !texture->create( image ) )
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

	auto texture = std::make_unique<DMTexture>( id, name );
	if( !texture->create( image ) )
		return false;

	return insertResource( std::move( texture ) );
}

bool DMTextureStorage::createNoise( uint32_t id, const std::string& name, uint32_t size )
{
	if( exists( id ) )
		return true;

	std::vector<uint8_t> pixels( size * size );
	std::mt19937 generator( 1 );
	std::uniform_int_distribution<int> dist( 0, 255 );
	for( uint8_t& pixel : pixels )
		pixel = static_cast<uint8_t>( dist( generator ) );

	TextureDesc desc;
	desc.width = size;
	desc.height = size;
	desc.format = DXGI_FORMAT_R8_SNORM;
	desc.usage = TextureUsage::shaderResource;
	TextureData data;
	data.data = pixels.data();
	data.rowPitch = size;

	auto texture = std::make_unique<DMTexture>( id, name );
	if( !texture->create( desc, data ) )
		return false;

	return insertResource( std::move( texture ) );
}

bool DMTextureStorage::createDefaults()
{
	// Цвета — R8G8B8A8 в памяти: R, G, B, A. Плоская нормаль (0.5, 0.5, 1) в касательном пространстве
	return createNoise( noiseId, "monochrome_noise", 256 ) &&
		   createSolid( whiteId, "default_white", 0xFFFFFFFF ) &&
		   createSolid( flatNormalId, "default_normal", 0xFFFF8080 );
}

bool DMTextureStorage::load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap, bool sRGB,
							  float preserveAlphaCoverage )
{
	if( exists( id ) || exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + file;

	ScratchImage baseImage;
	if( !ImageFile::load( fullPath, baseImage ) )
		return false;

	// Цветовое пространство задаёт колонка Textures.sRGB, а не метаданные файла: байты те же, меняется только
	// толкование формата (цвет — sRGB, данные вроде нормалей и масок — линейно). До генерации мипов,
	// чтобы они фильтровались в линейном пространстве
	const DXGI_FORMAT fileFormat = baseImage.GetMetadata().format;
	baseImage.OverrideFormat( sRGB ? MakeSRGB( fileFormat ) : MakeLinear( fileFormat ) );

	if( generateMipMap && !ImageMips::generate( baseImage, preserveAlphaCoverage ) && preserveAlphaCoverage > 0.0f )
		LOG( "Can`t preserve alpha coverage in mips of texture " + name + ": the format is not 8-bit RGBA" );

	auto texture = std::make_unique<DMTexture>( id, name );
	if( !texture->create( baseImage ) )
		return false;

	return insertResource( std::move( texture ) );
}

}