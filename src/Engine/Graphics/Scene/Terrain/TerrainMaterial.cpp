#include "TerrainMaterial.h"
#include <cstring>
#include <DirectXTex.h>
#include "System.h"
#include "DBConnector.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "TextureObjects\TextureLoader.h"

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

// Загружает файл (путь относительно каталога текстур) и приводит его первое изображение к несжатому
// R8G8B8A8_UNORM размером width × height. Нулевые width и height заполняются размером файла
bool loadImage( const std::string& file, size_t& width, size_t& height, ScratchImage& image )
{
	if( file.empty() )
		return false;

	TextureLoader loader;
	ScratchImage loaded;
	if( !loader.loadFromFile( ( GS::System::textures().path() + "\\" + file ).c_str(), loaded ) )
		return false;

	const Image* source = loaded.GetImage( 0, 0, 0 );

	ScratchImage decompressed;
	if( IsCompressed( source->format ) )
	{
		if( FAILED( Decompress( *source, DXGI_FORMAT_R8G8B8A8_UNORM, decompressed ) ) )
			return false;
		source = decompressed.GetImage( 0, 0, 0 );
	}

	ScratchImage converted;
	if( source->format != DXGI_FORMAT_R8G8B8A8_UNORM )
	{
		if( FAILED( Convert( *source, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted ) ) )
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

	return SUCCEEDED( image.InitializeFromImage( *source ) );
}

bool createArraySRV( const ScratchImage& layers, com_unique_ptr<ID3D11ShaderResourceView>& srv )
{
	ID3D11Resource* resource = nullptr;
	if( FAILED( CreateTexture( DMD3D::instance().GetDevice(), layers.GetImages(), layers.GetImageCount(), layers.GetMetadata(), &resource ) ) )
		return false;
	com_unique_ptr<ID3D11Resource> texture( resource );

	// Вид массива задаётся явно: для массива из одного слоя DirectXTex создал бы вид обычной текстуры
	D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = layers.GetMetadata().format;
	desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	desc.Texture2DArray.MipLevels = static_cast<UINT>( layers.GetMetadata().mipLevels );
	desc.Texture2DArray.ArraySize = static_cast<UINT>( layers.GetMetadata().arraySize );

	ID3D11ShaderResourceView* view = nullptr;
	if( FAILED( DMD3D::instance().GetDevice()->CreateShaderResourceView( resource, &desc, &view ) ) )
		return false;
	srv.reset( view );
	return true;
}

// Массив текстур из файлов слоёв; пустое имя — слой не описан
bool buildArray( const std::vector<std::string>& files, Fallback fallback, com_unique_ptr<ID3D11ShaderResourceView>& srv )
{
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

	ScratchImage mips;
	if( FAILED( GenerateMipMaps( layers.GetImages(), layers.GetImageCount(), layers.GetMetadata(), mipFilter, 0, mips ) ) )
		return false;

	return createArraySRV( mips, srv );
}

bool loadSplatMap( const std::string& file, com_unique_ptr<ID3D11ShaderResourceView>& srv )
{
	size_t width = 0;
	size_t height = 0;
	ScratchImage image;
	if( !loadImage( file, width, height, image ) )
	{
		LOG( "Terrain material: can`t load splat map " + file + ", placeholder is used" );
		if( FAILED( image.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, fallbackSize, fallbackSize, 1, 1 ) ) )
			return false;
		fill( *image.GetImage( 0, 0, 0 ), Fallback::firstLayer );
	}

	ScratchImage mips;
	if( FAILED( GenerateMipMaps( *image.GetImage( 0, 0, 0 ), mipFilter, 0, mips ) ) )
		return false;

	ID3D11ShaderResourceView* view = nullptr;
	if( FAILED( CreateShaderResourceView( DMD3D::instance().GetDevice(), mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), &view ) ) )
		return false;
	srv.reset( view );
	return true;
}

}

namespace GS
{

bool TerrainMaterial::initialize( uint32_t terrainId, const std::string& splatMap )
{
	std::vector<Layer> layers;
	if( !loadLayers( terrainId, layers ) )
		return false;

	std::vector<std::string> albedoFiles;
	std::vector<std::string> normalFiles;
	float scale[maxLayers] = {};
	for( uint32_t i = 0; i < maxLayers; ++i )
	{
		albedoFiles.push_back( layers[i].albedo );
		normalFiles.push_back( layers[i].normal );
		scale[i] = layers[i].tiling > 0.0f ? 1.0f / layers[i].tiling : 1.0f;
	}
	m_layerScale = XMFLOAT4( scale[0], scale[1], scale[2], scale[3] );

	if( !loadSplatMap( splatMap, m_splatMap ) ||
		!buildArray( albedoFiles, Fallback::checker, m_albedoHeight ) ||
		!buildArray( normalFiles, Fallback::flatNormal, m_normalRoughness ) )
	{
		LOG( "Terrain material: can`t create textures" );
		return false;
	}

	return true;
}

bool TerrainMaterial::loadLayers( uint32_t terrainId, std::vector<Layer>& layers )
{
	layers.assign( maxLayers, {} );

	try
	{
		SQLite::Statement query( dbConnect().db(), "select layer, name, albedo, normal, tiling from TerrainLayers where terrain = :terrain" );
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

	for( uint32_t i = 0; i < maxLayers; ++i )
	{
		if( layers[i].albedo.empty() && layers[i].normal.empty() )
			LOG( "Terrain material: layer " + std::to_string( i ) + " is not described in TerrainLayers, placeholder is used" );
	}

	return true;
}

void TerrainMaterial::bind() const
{
	DMD3D::instance().setSRV( SRVType::ps, 1, m_splatMap );
	DMD3D::instance().setSRV( SRVType::ps, 2, m_albedoHeight );
	DMD3D::instance().setSRV( SRVType::ps, 3, m_normalRoughness );
	DMD3D::instance().setSRV( SRVType::ps, 4, System::textures().get( "monohromeNoise" )->srv() );
}

}
