#include "TerrainMaterial.h"
#include "TerrainEdits.h"
#include "D3D\TextureImages.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <DirectXTex.h>
#include "System.h"
#include "DBConnector.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "Texture\ImageFile.h"

using namespace DirectX;

static_assert( GS::TerrainEditCoverage::paintLayers == GS::TerrainMaterial::maxLayers, "terrain edits paint TerrainMaterial layers" );

namespace
{

constexpr size_t fallbackSize = 256;
constexpr size_t checkerCell = 32;
constexpr TEX_FILTER_FLAGS mipFilter = TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC;

enum class Fallback
{
	checker,	// альбедо: пурпурно-чёрная шахматка, как у отсутствующих текстур
	flatNormal,	// нормаль (0, 0, 1), шероховатость 0.9
	firstLayer	// splat-карта: весь вес у слоя 0
};

void fill( const Image& image, Fallback fallback )
{
	// R8G8B8A8 в памяти: R, G, B, A
	const uint32_t magenta = 0x80FF00FF;	// высота 0.5
	const uint32_t black = 0x80000000;
	const uint32_t flatNormal = 0xE6FF8080;
	const uint32_t firstLayer = 0x000000FF;

	for( size_t y = 0; y < image.height; ++y )
	{
		uint32_t* row = reinterpret_cast<uint32_t*>( image.pixels + y * image.rowPitch );
		for( size_t x = 0; x < image.width; ++x )
		{
			switch( fallback )
			{
				case Fallback::checker:
					row[x] = ( ( x / checkerCell + y / checkerCell ) % 2 ) ? black : magenta;
					break;
				case Fallback::flatNormal:
					row[x] = flatNormal;
					break;
				case Fallback::firstLayer:
					row[x] = firstLayer;
					break;
			}
		}
	}
}

// Загружает файл (путь относительно каталога текстур) и приводит его первое изображение к несжатому R8G8B8A8
// размером width × height — байтами как в файле: цветовое пространство задаёт назначение (альбедо слоя — sRGB,
// buildArray), а не метка формата файла, как у Textures.sRGB. Нулевые width и height заполняются размером файла
bool convertImage( const Image& loadedImage, size_t& width, size_t& height, ScratchImage& image );

bool loadImage( const std::string& file, size_t& width, size_t& height, ScratchImage& image )
{
	if( file.empty() )
		return false;

	ScratchImage loaded;
	if( !ImageFile::load( GS::System::textures().path() + "\\" + file, loaded ) )
		return false;

	return convertImage( *loaded.GetImage( 0, 0, 0 ), width, height, image );
}

// Картинка файла → несжатый R8G8B8A8 размером width × height, байтами как в файле (см. loadImage)
bool convertImage( const Image& loadedImage, size_t& width, size_t& height, ScratchImage& image )
{
	const Image* source = &loadedImage;
	// Файл с меткой sRGB — в R8G8B8A8_UNORM_SRGB, иначе в UNORM: так преобразования не трогают значения байтов
	const DXGI_FORMAT target = IsSRGB( source->format ) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

	ScratchImage decompressed;
	if( IsCompressed( source->format ) )
	{
		if( FAILED( Decompress( *source, target, decompressed ) ) )
			return false;
		source = decompressed.GetImage( 0, 0, 0 );
	}

	ScratchImage converted;
	if( source->format != target )
	{
		if( FAILED( Convert( *source, target, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted ) ) )
			return false;
		source = converted.GetImage( 0, 0, 0 );
	}

	if( width == 0 || height == 0 )
	{
		width = source->width;
		height = source->height;
	}

	ScratchImage resized;
	if( source->width != width || source->height != height )
	{
		if( FAILED( Resize( *source, width, height, TEX_FILTER_DEFAULT | TEX_FILTER_SEPARATE_ALPHA, resized ) ) )
			return false;
		source = resized.GetImage( 0, 0, 0 );
	}

	return SUCCEEDED( image.InitializeFromImage( *source ) ) && SUCCEEDED( image.OverrideFormat( DXGI_FORMAT_R8G8B8A8_UNORM ) );
}

bool createArraySRV( const ScratchImage& layers, Texture& texture, ShaderView& srv )
{
	// Вид массива задаётся явно: для массива из одного слоя DirectXTex создал бы вид обычной текстуры
	return GpuImages::createTexture( layers, texture, srv, TextureViewDesc::Kind::texture2DArray );
}

// Массив из готовых мипов файлов слоёв (Tools/pack_terrain_layer.py и генератор пишут полную цепочку): у всех слоёв
// один размер, R8G8B8A8 и полная цепочка — без GenerateMipMaps, которые на CPU стоят секунды (в Debug — ещё больше)
bool buildArrayFromFileMips( const std::vector<std::string>& files, bool srgb, Texture& texture, ShaderView& srv )
{
	std::vector<ScratchImage> images( files.size() );
	for( size_t i = 0; i < files.size(); ++i )
	{
		if( files[i].empty() || !ImageFile::load( GS::System::textures().path() + "\\" + files[i], images[i] ) )
			return false;
		const TexMetadata& meta = images[i].GetMetadata();
		const TexMetadata& first = images[0].GetMetadata();
		size_t fullChain = 1;
		for( size_t size = std::max( meta.width, meta.height ); size > 1; size /= 2 )
			++fullChain;
		const bool rgba8 = meta.format == DXGI_FORMAT_R8G8B8A8_UNORM || meta.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		if( !rgba8 || meta.arraySize != 1 || meta.mipLevels != fullChain || meta.width != first.width ||
			meta.height != first.height )
			return false;
	}

	const TexMetadata& meta = images[0].GetMetadata();
	ScratchImage layers;
	if( FAILED( layers.Initialize2D( srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, meta.width,
									 meta.height, files.size(), meta.mipLevels ) ) )
		return false;
	for( size_t i = 0; i < files.size(); ++i )
	{
		for( size_t mip = 0; mip < meta.mipLevels; ++mip )
		{
			const Image& source = *images[i].GetImage( mip, 0, 0 );
			const Image& target = *layers.GetImage( mip, i, 0 );
			for( size_t y = 0; y < source.height; ++y )
				std::memcpy( target.pixels + y * target.rowPitch, source.pixels + y * source.rowPitch, source.width * 4 );
		}
	}
	return createArraySRV( layers, texture, srv );
}

// Массив текстур из файлов слоёв; пустое имя — слой не описан. srgb — RGB в sRGB (альбедо): массив R8G8B8A8_UNORM_SRGB,
// выборка возвращает линейный цвет, мипы фильтруются в линейном; альфа (высота) остаётся линейной
bool buildArray( const std::vector<std::string>& files, Fallback fallback, bool srgb, Texture& texture, ShaderView& srv )
{
	if( buildArrayFromFileMips( files, srgb, texture, srv ) )
		return true;

	std::vector<ScratchImage> images( files.size() );
	std::vector<bool> loaded( files.size(), false );
	size_t width = 0;
	size_t height = 0;

	for( size_t i = 0; i < files.size(); ++i )
	{
		loaded[i] = loadImage( files[i], width, height, images[i] );
		if( !loaded[i] && !files[i].empty() )
			LOG( "Terrain material: can`t load " + files[i] + ", placeholder is used" );
	}

	if( width == 0 || height == 0 )
		width = height = fallbackSize;

	ScratchImage layers;
	if( FAILED( layers.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, width, height, files.size(), 1 ) ) )
		return false;

	for( size_t i = 0; i < files.size(); ++i )
	{
		const Image& target = *layers.GetImage( 0, i, 0 );
		if( !loaded[i] )
		{
			fill( target, fallback );
			continue;
		}

		const Image& source = *images[i].GetImage( 0, 0, 0 );
		for( size_t y = 0; y < height; ++y )
			std::memcpy( target.pixels + y * target.rowPitch, source.pixels + y * source.rowPitch, width * 4 );
	}

	if( srgb && FAILED( layers.OverrideFormat( DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) ) )
		return false;

	ScratchImage mips;
	if( FAILED( GenerateMipMaps( layers.GetImages(), layers.GetImageCount(), layers.GetMetadata(), mipFilter, 0, mips ) ) )
		return false;

	return createArraySRV( mips, texture, srv );
}

// Покраска правками рельефа (TerrainEdits.paint_layer, как Paint Layer у Landscape Splines): вес слоя i = вес файла ×
// remaining + paint[i] по полям покрытия в UV текселя splat-карты; слоёв дальше layerCount нет в массивах — их покраска
// пропускается
void paintSplatMap( ScratchImage& splat, const GS::TerrainEditCoverage& coverage, uint32_t layerCount )
{
	const TexMetadata& metadata = splat.GetMetadata();
	const size_t width = metadata.width;
	const size_t height = metadata.height;
	for( size_t y = 0; y < height; ++y )
	{
		const float v = ( y + 0.5f ) / height;
		for( size_t x = 0; x < width; ++x )
		{
			const float u = ( x + 0.5f ) / width;
			const float remaining = coverage.sample( coverage.remaining, u, v );
			if( remaining >= 1.0f )
				continue;
			for( size_t slice = 0; slice < GS::TerrainMaterial::splatSlices; ++slice )
			{
				uint8_t* texel = splat.GetImage( 0, slice, 0 )->pixels + y * splat.GetImage( 0, slice, 0 )->rowPitch + x * 4;
				for( size_t c = 0; c < 4; ++c )
				{
					const size_t layer = slice * 4 + c;
					const float painted = layer < layerCount ? coverage.sample( coverage.paint[layer], u, v ) : 0.0f;
					const float weight = texel[c] / 255.0f * remaining + painted;
					texel[c] = static_cast<uint8_t>( std::lround( std::clamp( weight, 0.0f, 1.0f ) * 255.0f ) );
				}
			}
		}
	}
}

// Splat-карта — массив из splatSlices срезов RGBA (срез s — веса слоёв 4s…4s + 3). Срезов в файле меньше — остальные
// нули: прежний файл из одной картинки читается как раньше. Нет файла — весь вес у слоя 0. Покраска правками — поверх
bool loadSplatMap( const std::string& file, const GS::TerrainEditCoverage& coverage, uint32_t layerCount, Texture& texture,
				   ShaderView& srv )
{
	constexpr size_t slices = GS::TerrainMaterial::splatSlices;
	size_t width = 0;
	size_t height = 0;
	std::vector<ScratchImage> images;
	ScratchImage loaded;
	if( !file.empty() && ImageFile::load( GS::System::textures().path() + "\\" + file, loaded ) )
	{
		const size_t count = std::min( loaded.GetMetadata().arraySize, slices );
		images.resize( count );
		for( size_t i = 0; i < count; ++i )
		{
			if( !convertImage( *loaded.GetImage( 0, i, 0 ), width, height, images[i] ) )
			{
				images.clear();
				break;
			}
		}
	}
	if( images.empty() )
	{
		LOG( "Terrain material: can`t load splat map " + file + ", placeholder is used" );
		width = height = fallbackSize;
	}

	ScratchImage splat;
	if( FAILED( splat.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, width, height, slices, 1 ) ) )
		return false;
	for( size_t i = 0; i < slices; ++i )
	{
		const Image& target = *splat.GetImage( 0, i, 0 );
		if( i < images.size() )
		{
			const Image& source = *images[i].GetImage( 0, 0, 0 );
			for( size_t y = 0; y < height; ++y )
				std::memcpy( target.pixels + y * target.rowPitch, source.pixels + y * source.rowPitch, width * 4 );
		}
		else if( i == 0 )
			fill( target, Fallback::firstLayer );
		else
			for( size_t y = 0; y < height; ++y )
				std::memset( target.pixels + y * target.rowPitch, 0, width * 4 );
	}
	if( coverage.paints() )
		paintSplatMap( splat, coverage, layerCount );

	ScratchImage mips;
	if( FAILED( GenerateMipMaps( splat.GetImages(), splat.GetImageCount(), splat.GetMetadata(), mipFilter, 0, mips ) ) )
		return false;
	return createArraySRV( mips, texture, srv );
}

}

namespace GS
{

bool TerrainMaterial::initialize( uint32_t terrainId, const std::string& splatMap, const TerrainEditCoverage& coverage )
{
	std::vector<Layer> layers;
	if( !loadLayers( terrainId, layers ) )
		return false;

	// Массивы — по числу описанных слоёв, а не по maxLayers: срез 2048² с мипами — ~21 МБ на массив
	m_layerCount = static_cast<uint32_t>( layers.size() );
	std::vector<std::string> albedoFiles;
	std::vector<std::string> normalFiles;
	float scale[maxLayers] = {};
	for( uint32_t i = 0; i < m_layerCount; ++i )
	{
		albedoFiles.push_back( layers[i].albedo );
		normalFiles.push_back( layers[i].normal );
		scale[i] = layers[i].tiling > 0.0f ? 1.0f / layers[i].tiling : 1.0f;
	}
	for( uint32_t s = 0; s < splatSlices; ++s )
		m_layerScale[s] = XMFLOAT4( scale[4 * s], scale[4 * s + 1], scale[4 * s + 2], scale[4 * s + 3] );

	for( uint32_t layer = m_layerCount; layer < TerrainEditCoverage::paintLayers; ++layer )
		if( !coverage.paint[layer].empty() )
			LOG( "Terrain material: terrain edits paint layer " + std::to_string( layer ) + ", which TerrainLayers does not describe" );

	if( !loadSplatMap( splatMap, coverage, m_layerCount, m_splatMapTexture, m_splatMap ) ||
		!buildArray( albedoFiles, Fallback::checker, true, m_albedoHeightTexture, m_albedoHeight ) ||
		!buildArray( normalFiles, Fallback::flatNormal, false, m_normalRoughnessTexture, m_normalRoughness ) )
	{
		LOG( "Terrain material: can`t create textures" );
		return false;
	}
	DMD3D::instance().setName( m_splatMapTexture, "Terrain splat map" );
	DMD3D::instance().setName( m_albedoHeightTexture, "Terrain albedo + height" );
	DMD3D::instance().setName( m_normalRoughnessTexture, "Terrain normal + roughness" );

	return true;
}

bool TerrainMaterial::loadLayers( uint32_t terrainId, std::vector<Layer>& layers )
{
	layers.assign( maxLayers, {} );

	try
	{
		SQLite::Statement query( DBConnector::instance().db(), "select layer, name, albedo, normal, tiling from TerrainLayers where terrain = :terrain" );
		query.bind( ":terrain", terrainId );

		while( query.executeStep() )
		{
			const int index = query.getColumn( "layer" ).getInt();
			if( index < 0 || index >= static_cast<int>( maxLayers ) )
			{
				LOG( "Terrain material: layer " + std::to_string( index ) + " is out of range 0.." + std::to_string( maxLayers - 1 ) );
				continue;
			}

			Layer& layer = layers[index];
			layer.name = query.getColumn( "name" ).getString();
			layer.albedo = query.getColumn( "albedo" ).getString();
			layer.normal = query.getColumn( "normal" ).getString();
			layer.tiling = static_cast<float>( query.getColumn( "tiling" ).getDouble() );
		}
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Terrain material: " ) + e.what() );
		return false;
	}

	// Слоёв — до наибольшего описанного; пропуск в номерах — заглушка
	while( layers.size() > 1 && layers.back().albedo.empty() && layers.back().normal.empty() )
		layers.pop_back();
	for( uint32_t i = 0; i < layers.size(); ++i )
	{
		if( layers[i].albedo.empty() && layers[i].normal.empty() )
			LOG( "Terrain material: layer " + std::to_string( i ) + " is not described in TerrainLayers, placeholder is used" );
	}

	return true;
}

void TerrainMaterial::bind() const
{
	DMD3D::instance().setSRV( 1, m_splatMap );
	DMD3D::instance().setSRV( 2, m_albedoHeight );
	DMD3D::instance().setSRV( 3, m_normalRoughness );
	DMD3D::instance().setSRV( 4, System::textures().get( DMTextureStorage::noiseId )->srv() );
}

}
