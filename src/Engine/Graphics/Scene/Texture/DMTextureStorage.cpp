#include "DMTextureStorage.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include "ImageFile.h"
#include "ImageMips.h"
#include "Logger\Logger.h"
#include "Utils\Fingerprint.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr const char* cookedDirectory = "cache\\textures";

}

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

bool DMTextureStorage::setGenerated( const std::string& name, const TextureDesc& desc, const TextureData& data )
{
	// Есть — пересоздать в том же объекте (вид srv() остаётся членом того же DMTexture), нет — новый id вне base.db3
	if( exists( name ) )
		return get( id( name ) )->create( desc, data );
	auto texture = std::make_unique<DMTexture>( freeId(), name );
	if( !texture->create( desc, data ) )
		return false;
	return insertResource( std::move( texture ) );
}

TextureCompression DMTextureStorage::compressionFromName( const std::string& name )
{
	if( name == "default" )
		return TextureCompression::standard;
	if( name == "normalmap" )
		return TextureCompression::normalMap;
	return TextureCompression::none;
}

bool DMTextureStorage::load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap, bool sRGB,
							  float preserveAlphaCoverage, TextureCompression compression )
{
	if( exists( id ) || exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + file;
	std::error_code error;
	if( !std::filesystem::exists( fullPath, error ) )
	{
		LOG( "Can`t find texture file " + fullPath );
		return false;
	}

	// Отпечаток источника и того, как он готовится: ключ готовой текстуры в кэше и кэшей, которые зависят от картинки
	// (кадры импостеров)
	Fingerprint stamp;
	stamp.add( fullPath.data(), fullPath.size() );
	stamp.add( static_cast<uint64_t>( std::filesystem::file_size( fullPath, error ) ) );
	stamp.add( std::filesystem::last_write_time( fullPath, error ).time_since_epoch().count() );
	stamp.add( sRGB );
	stamp.add( generateMipMap );
	stamp.add( preserveAlphaCoverage );
	stamp.add( compression );
	stamp.add( cookVersion );

	// Готовить есть что (мипы, сжатие) — готовая текстура в кэше: <имя>.<отпечаток>.dds, в имени точек нет, поэтому
	// прежние варианты этой текстуры — файлы «<имя>.*»
	const bool cook = generateMipMap || compression != TextureCompression::none;
	std::string cookedName = name;
	for( char& c : cookedName )
		if( !std::isalnum( static_cast<unsigned char>( c ) ) )
			c = '_';
	char stampText[20];
	std::snprintf( stampText, sizeof( stampText ), "%016llx", static_cast<unsigned long long>( stamp.value ) );
	const std::string cookedPath = std::string( cookedDirectory ) + "\\" + cookedName + "." + stampText + ".dds";

	ScratchImage baseImage;
	const bool cached = cook && std::filesystem::exists( cookedPath, error ) && ImageFile::load( cookedPath, baseImage );
	if( !cached )
	{
		const auto start = std::chrono::steady_clock::now();
		if( !ImageFile::load( fullPath, baseImage ) )
			return false;

		// Цветовое пространство задаёт колонка Textures.sRGB, а не метаданные файла: байты те же, меняется только
		// толкование формата (цвет — sRGB, данные вроде нормалей и масок — линейно). До генерации мипов,
		// чтобы они фильтровались в линейном пространстве
		const DXGI_FORMAT fileFormat = baseImage.GetMetadata().format;
		baseImage.OverrideFormat( sRGB ? MakeSRGB( fileFormat ) : MakeLinear( fileFormat ) );

		if( generateMipMap && !ImageMips::generate( baseImage, preserveAlphaCoverage ) && preserveAlphaCoverage > 0.0f )
			LOG( "Can`t preserve alpha coverage in mips of texture " + name + ": the format is not 8-bit RGBA" );

		// Сжатие блоками 4 × 4: верхний мип — кратный 4 (правило D3D12); иначе — как есть
		const TexMetadata& metadata = baseImage.GetMetadata();
		if( compression != TextureCompression::none && !IsCompressed( metadata.format ) && metadata.width % 4 == 0 && metadata.height % 4 == 0 )
		{
			const DXGI_FORMAT format = compression == TextureCompression::normalMap ? DXGI_FORMAT_BC5_UNORM :
									   sRGB ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
			ScratchImage compressed;
			if( SUCCEEDED( Compress( baseImage.GetImages(), baseImage.GetImageCount(), metadata, format,
									 TEX_COMPRESS_PARALLEL | TEX_COMPRESS_BC7_QUICK, TEX_THRESHOLD_DEFAULT, compressed ) ) )
				baseImage = std::move( compressed );
			else
				LOG( "Can`t compress texture " + name + ", it stays uncompressed" );
		}
		else if( compression != TextureCompression::none && !IsCompressed( metadata.format ) )
			LOG( "Texture " + name + " is not compressed: its size is not a multiple of 4" );

		if( cook )
		{
			std::filesystem::create_directories( cookedDirectory, error );
			const std::string prefix = cookedName + ".";
			for( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( cookedDirectory, error ) )
				if( entry.is_regular_file() && entry.path().filename().string().rfind( prefix, 0 ) == 0 )
					std::filesystem::remove( entry.path(), error );
			const std::wstring wide( cookedPath.begin(), cookedPath.end() );
			if( FAILED( SaveToDDSFile( baseImage.GetImages(), baseImage.GetImageCount(), baseImage.GetMetadata(), DDS_FLAGS_NONE, wide.c_str() ) ) )
				LOG( "Can`t write the cooked texture " + cookedPath );
			const auto ms = std::chrono::duration<float, std::milli>( std::chrono::steady_clock::now() - start ).count();
			LOG( "Texture " + name + " cooked to " + cookedPath + " (" + std::to_string( baseImage.GetMetadata().width ) + "x" +
				 std::to_string( baseImage.GetMetadata().height ) + ", " + std::to_string( baseImage.GetMetadata().mipLevels ) + " mips, " +
				 ( IsCompressed( baseImage.GetMetadata().format ) ? "compressed" : "uncompressed" ) + "), ms: " + std::to_string( ms ) );
		}
	}

	auto texture = std::make_unique<DMTexture>( id, name );
	if( !texture->create( baseImage ) )
		return false;
	texture->setSourceStamp( stamp.value );

	return insertResource( std::move( texture ) );
}

}