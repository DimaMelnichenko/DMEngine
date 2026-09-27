#include "DMTextureStorage.h"
#include "DDSTexture.h"
#include "Logger\Logger.h"

namespace
{

// Мипы с сохранением покрытия альфы (Castaño 2010, как Mip Maps Preserve Coverage в Unity): альфа мипа умножается
// на такое число, чтобы доля текселей выше порога совпала с нулевым мипом. Число находится по гистограмме альфы мипа
// (8 бит — 256 столбцов) за один проход, а не подбором с субсэмплингом, как ScaleMipMapsAlphaForCoverage в DirectXTex
// (у той — секунды на 2048²). Форматы — RGBA / BGRA по 8 бит, альфа — четвёртый байт
bool preserveAlphaCoverage( const ScratchImage& image, float cutoff )
{
	const TexMetadata& metadata = image.GetMetadata();
	const DXGI_FORMAT format = MakeLinear( metadata.format );
	if( format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_B8G8R8A8_UNORM )
		return false;

	auto histogram = []( const Image& mip, uint64_t( &counts )[256] )
	{
		std::fill( std::begin( counts ), std::end( counts ), 0 );
		for( size_t y = 0; y < mip.height; ++y )
		{
			const uint8_t* row = mip.pixels + y * mip.rowPitch;
			for( size_t x = 0; x < mip.width; ++x )
				++counts[row[x * 4 + 3]];
		}
	};

	const float threshold = cutoff * 255.0f;	// тексель виден, если альфа (в байтах) больше
	for( size_t item = 0; item < metadata.arraySize; ++item )
	{
		uint64_t counts[256];
		const Image& base = *image.GetImage( 0, item, 0 );
		histogram( base, counts );
		uint64_t covered = 0;
		for( int value = 0; value < 256; ++value )
			covered += value > threshold ? counts[value] : 0;
		const double coverage = double( covered ) / double( base.width * base.height );
		if( covered == 0 )
			continue;

		for( size_t level = 1; level < metadata.mipLevels; ++level )
		{
			const Image& mip = *image.GetImage( level, item, 0 );
			histogram( mip, counts );
			// Наименьшая альфа b, начиная с которой тексели мипа должны остаться видимыми
			const double target = coverage * double( mip.width * mip.height );
			uint64_t above = 0;
			int b = 256;
			while( b > 1 && double( above ) < target - 0.5 )
				above += counts[--b];
			if( b > 255 )
				continue;
			// b проходит порог, b − 1 — нет
			const float scale = threshold / ( float( b ) - 0.5f );
			for( size_t y = 0; y < mip.height; ++y )
			{
				uint8_t* row = mip.pixels + y * mip.rowPitch;
				for( size_t x = 0; x < mip.width; ++x )
					row[x * 4 + 3] = static_cast<uint8_t>( std::min( 255.0f, row[x * 4 + 3] * scale + 0.5f ) );
			}
		}
	}
	return true;
}

}

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

bool DMTextureStorage::load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap, bool sRGB,
							  float preserveAlphaCoverage )
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

		// При усреднении тонкие травинки и лепестки уходят под порог отсечения и вдали тают
		if( SUCCEEDED( hr ) && preserveAlphaCoverage > 0.0f && !::preserveAlphaCoverage( baseImage, preserveAlphaCoverage ) )
			LOG( "Can`t preserve alpha coverage in mips of texture " + name + ": the format is not 8-bit RGBA" );
	}

	texture.reset( new DDSTexture( id, name, std::move( baseImage ) ) );

	DXGI_FORMAT format = baseImage.GetMetadata().format;

	if( !texture->createSRV() )
		return false;

	insertResource( std::move( texture ) );

	return true;
}

}