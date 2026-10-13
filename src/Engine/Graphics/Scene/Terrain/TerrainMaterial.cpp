#include "TerrainMaterial.h"
#include "TerrainEdits.h"
#include "TerrainMasks.h"
#include "D3D\TextureImages.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <DirectXTex.h>
#include "System.h"
#include "DBConnector.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "Texture\ImageFile.h"
#include "Utils\Fingerprint.h"

using namespace DirectX;

static_assert( GS::TerrainEditCoverage::paintLayers == GS::TerrainMaterial::maxLayers, "terrain edits paint TerrainMaterial layers" );
static_assert( GS::TerrainMasks::splatLayers == GS::TerrainMaterial::maxLayers, "splat layers" );

namespace
{

constexpr size_t fallbackSize = 256;
constexpr size_t checkerCell = 32;
constexpr TEX_FILTER_FLAGS mipFilter = TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC;
// Готовые массивы слоёв (мипы, BC7) — рядом с готовыми текстурами моделей (DMTextureStorage::load), как cook в UE.
// Меняется сборка массива или сжатие — поднять версию: массивы приготовятся заново
constexpr const char* cookedDirectory = "cache\\textures";
constexpr uint32_t cookVersion = 1;

enum class Fallback
{
	checker,	// альбедо: пурпурно-чёрная шахматка, как у отсутствующих текстур
	flatNormal,	// нормаль (0, 0, 1), AO 1, шероховатость 0.9
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
bool loadArrayFromFileMips( const std::vector<std::string>& files, bool srgb, ScratchImage& layers )
{
	std::vector<ScratchImage> images( files.size() );
	SlowTask task( static_cast<float>( files.size() ) );
	for( size_t i = 0; i < files.size(); ++i )
	{
		task.enterProgressFrame( 1.0f, files[i] );
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
	return true;
}

// Массив текстур из файлов слоёв с мипами (несжатый); пустое имя — слой не описан. srgb — RGB в sRGB (альбедо): массив
// R8G8B8A8_UNORM_SRGB, выборка возвращает линейный цвет, мипы фильтруются в линейном; альфа (высота) остаётся линейной
bool loadArray( const std::vector<std::string>& files, Fallback fallback, bool srgb, ScratchImage& result )
{
	if( loadArrayFromFileMips( files, srgb, result ) )
		return true;

	std::vector<ScratchImage> images( files.size() );
	std::vector<bool> loaded( files.size(), false );
	size_t width = 0;
	size_t height = 0;

	// Запасной путь (у слоя нет файла с мипами): файлы, затем мипы на CPU — в Debug секунды
	SlowTask task( static_cast<float>( files.size() ) * 2.0f );
	for( size_t i = 0; i < files.size(); ++i )
	{
		task.enterProgressFrame( 1.0f, files[i] );
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

	task.enterProgressFrame( static_cast<float>( files.size() ), "Mipmaps" );
	return SUCCEEDED( GenerateMipMaps( layers.GetImages(), layers.GetImageCount(), layers.GetMetadata(), mipFilter, 0, result ) );
}

// Массив слоёв на GPU в BC7 (sRGB — у альбедо; альфа — высота или шероховатость — линейная в обоих): в 4 раза меньше
// памяти и чтения в terrain.ps. Готовый массив — в кэше cache\textures\<name>.<отпечаток>.dds: отпечаток — файлы слоёв
// (путь, размер, время правки), sRGB и cookVersion; сменились — массив собирается и сжимается заново, прежний удаляется
bool buildArray( const std::vector<std::string>& files, Fallback fallback, bool srgb, const char* name, Texture& texture, ShaderView& srv )
{
	Fingerprint stamp;
	std::error_code error;
	for( const std::string& file : files )
	{
		const std::string path = GS::System::textures().path() + "\\" + file;
		stamp.add( path.data(), path.size() );
		stamp.add( static_cast<uint64_t>( std::filesystem::file_size( path, error ) ) );
		stamp.add( std::filesystem::last_write_time( path, error ).time_since_epoch().count() );
	}
	stamp.add( srgb );
	stamp.add( fallback );
	stamp.add( cookVersion );
	char stampText[20];
	std::snprintf( stampText, sizeof( stampText ), "%016llx", static_cast<unsigned long long>( stamp.value ) );
	const std::string cookedPath = std::string( cookedDirectory ) + "\\" + name + "." + stampText + ".dds";

	const DXGI_FORMAT compressed = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
	ScratchImage layers;
	if( std::filesystem::exists( cookedPath, error ) && ImageFile::load( cookedPath, layers ) &&
		layers.GetMetadata().format == compressed && layers.GetMetadata().arraySize == files.size() )
		return createArraySRV( layers, texture, srv );

	const auto start = std::chrono::steady_clock::now();
	if( !loadArray( files, fallback, srgb, layers ) )
		return false;
	// Сжатие блоками 4 × 4: сторона — кратная 4 (правило D3D12); иначе — как есть
	const TexMetadata& metadata = layers.GetMetadata();
	if( metadata.width % 4 == 0 && metadata.height % 4 == 0 )
	{
		ScratchImage bc7;
		if( SUCCEEDED( Compress( layers.GetImages(), layers.GetImageCount(), metadata, compressed,
								 TEX_COMPRESS_PARALLEL | TEX_COMPRESS_BC7_QUICK, TEX_THRESHOLD_DEFAULT, bc7 ) ) )
			layers = std::move( bc7 );
		else
			LOG( std::string( "Terrain material: can`t compress " ) + name + ", it stays uncompressed" );
	}
	std::filesystem::create_directories( cookedDirectory, error );
	const std::string prefix = std::string( name ) + ".";
	for( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( cookedDirectory, error ) )
		if( entry.is_regular_file() && entry.path().filename().string().rfind( prefix, 0 ) == 0 )
			std::filesystem::remove( entry.path(), error );
	const std::wstring wide( cookedPath.begin(), cookedPath.end() );
	if( FAILED( SaveToDDSFile( layers.GetImages(), layers.GetImageCount(), layers.GetMetadata(), DDS_FLAGS_NONE, wide.c_str() ) ) )
		LOG( "Terrain material: can`t write the cooked array " + cookedPath );
	const auto ms = std::chrono::duration<float, std::milli>( std::chrono::steady_clock::now() - start ).count();
	LOG( std::string( "Terrain material: " ) + name + " cooked to " + cookedPath + ", ms: " + std::to_string( ms ) );
	return createArraySRV( layers, texture, srv );
}

// Splat-карта — массив из splatTextureSlices срезов RGBA (срез s < splatSlices — веса слоёв 4s…4s + 3, последний — R
// мокрость); весов нет — весь вес у слоя 0
bool buildSplatMap( const GS::TerrainMaterial::SplatWeights& weights, Texture& texture, ShaderView& srv )
{
	constexpr size_t slices = GS::TerrainMaterial::splatTextureSlices;
	constexpr size_t weightSlices = GS::TerrainMaterial::splatSlices;
	const size_t size = weights.size ? weights.size : fallbackSize;

	ScratchImage splat;
	if( FAILED( splat.Initialize2D( DXGI_FORMAT_R8G8B8A8_UNORM, size, size, slices, 1 ) ) )
		return false;
	const auto store = []( float value ) { return static_cast<uint8_t>( std::lround( std::clamp( value, 0.0f, 1.0f ) * 255.0f ) ); };
	for( size_t slice = 0; slice < slices; ++slice )
	{
		const Image& target = *splat.GetImage( 0, slice, 0 );
		if( weights.size == 0 )
		{
			if( slice == 0 )
				fill( target, Fallback::firstLayer );
			else
				for( size_t y = 0; y < size; ++y )
					std::memset( target.pixels + y * target.rowPitch, 0, size * 4 );
			continue;
		}
		for( size_t y = 0; y < size; ++y )
		{
			uint8_t* texel = target.pixels + y * target.rowPitch;
			for( size_t x = 0; x < size; ++x, texel += 4 )
				for( size_t c = 0; c < 4; ++c )
				{
					const std::vector<float>& values = slice < weightSlices ? weights.layers[slice * 4 + c] : weights.wetness;
					texel[c] = ( slice < weightSlices || c == 0 ) && !values.empty() ? store( values[y * size + x] ) : 0;
				}
		}
	}

	ScratchImage mips;
	if( FAILED( GenerateMipMaps( splat.GetImages(), splat.GetImageCount(), splat.GetMetadata(), mipFilter, 0, mips ) ) )
		return false;
	return createArraySRV( mips, texture, srv );
}

}

namespace GS
{

void TerrainMaterial::setTiling( uint32_t layer, float tiling )
{
	if( layer >= m_layerCount )
		return;
	m_tiling[layer] = tiling;
	const float scale = tiling > 0.0f ? 1.0f / tiling : 1.0f;
	float* slice = &m_layerScale[layer / 4].x;
	slice[layer % 4] = scale;
}

void TerrainMaterial::setTint( uint32_t layer, const XMFLOAT3& tint )
{
	if( layer >= m_layerCount )
		return;
	m_tint[layer] = tint;
	m_layerTint[layer] = XMFLOAT4( tint.x, tint.y, tint.z, 0.0f );
}

void TerrainMaterial::setDisplacement( uint32_t layer, float displacement )
{
	if( layer >= m_layerCount )
		return;
	m_displacement[layer] = std::max( displacement, 0.0f );
	float* slice = &m_layerDisplacement[layer / 4].x;
	slice[layer % 4] = m_displacement[layer];
}

void TerrainMaterial::setOcclusion( uint32_t layer, float occlusion )
{
	if( layer >= m_layerCount )
		return;
	m_occlusion[layer] = std::clamp( occlusion, 0.0f, 1.0f );
	float* slice = &m_layerOcclusion[layer / 4].x;
	slice[layer % 4] = m_occlusion[layer];
}

float TerrainMaterial::maxDisplacement() const
{
	float result = 0.0f;
	for( float value : m_displacement )
		result = std::max( result, value );
	return result;
}

bool TerrainMaterial::initialize( uint32_t terrainId, const SplatWeights& weights )
{
	std::vector<Layer> layers;
	if( !loadLayers( terrainId, layers ) )
		return false;

	// Массивы — по числу описанных слоёв, а не по maxLayers: срез 2048² с мипами — ~21 МБ на массив
	m_layerCount = static_cast<uint32_t>( layers.size() );
	std::vector<std::string> albedoFiles;
	std::vector<std::string> normalFiles;
	m_layerNames.assign( m_layerCount, {} );
	m_tiling.assign( m_layerCount, 1.0f );
	m_tint.assign( m_layerCount, XMFLOAT3( 1.0f, 1.0f, 1.0f ) );
	m_layerTint.fill( XMFLOAT4( 1.0f, 1.0f, 1.0f, 0.0f ) );
	m_displacement.assign( m_layerCount, 0.0f );
	m_layerDisplacement.fill( XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f ) );
	m_occlusion.assign( m_layerCount, 1.0f );
	m_layerOcclusion.fill( XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f ) );
	for( uint32_t i = 0; i < m_layerCount; ++i )
	{
		albedoFiles.push_back( layers[i].albedo );
		normalFiles.push_back( layers[i].normal );
		m_layerNames[i] = layers[i].name;
		setTiling( i, layers[i].tiling );
		setTint( i, layers[i].tint );
		setDisplacement( i, layers[i].displacement );
		setOcclusion( i, layers[i].occlusion );
	}

	// Шаги прогресса загрузки — splat-карта и два массива слоёв (по файлу на шаг — buildArray)
	SlowTask task( 1.0f );
	task.enterProgressFrame( 0.1f, "Splat map" );
	bool created = buildSplatMap( weights, m_splatMapTexture, m_splatMap );
	task.enterProgressFrame( 0.45f, "Albedo" );
	created = created && buildArray( albedoFiles, Fallback::checker, true, "terrain_albedo_height", m_albedoHeightTexture, m_albedoHeight );
	task.enterProgressFrame( 0.45f, "Normals" );
	created = created && buildArray( normalFiles, Fallback::flatNormal, false, "terrain_normal_roughness", m_normalRoughnessTexture,
									 m_normalRoughness );
	if( !created )
	{
		LOG( "Terrain material: can`t create textures" );
		return false;
	}
	DMD3D::instance().setName( m_splatMapTexture, "Terrain splat map" );
	DMD3D::instance().setName( m_albedoHeightTexture, "Terrain albedo + height" );
	DMD3D::instance().setName( m_normalRoughnessTexture, "Terrain normal + roughness" );

	return true;
}

bool TerrainMaterial::repaint( const SplatWeights& weights )
{
	if( !buildSplatMap( weights, m_splatMapTexture, m_splatMap ) )
		return false;
	DMD3D::instance().setName( m_splatMapTexture, "Terrain splat map" );
	return true;
}

bool TerrainMaterial::loadLayers( uint32_t terrainId, std::vector<Layer>& layers )
{
	layers.assign( maxLayers, {} );

	try
	{
		SQLite::Statement query( DBConnector::instance().db(), "select * from TerrainLayers where terrain = :terrain" );
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
			// albedo_tint — "r,g,b"; колонки нет или NULL — текстура как есть; displacement — м, NULL — без смещения;
			// occlusion — сила AO, NULL — 1
			for( int column = 0; column < query.getColumnCount(); ++column )
			{
				const std::string name = query.getColumnName( column );
				if( query.getColumn( column ).isNull() )
					continue;
				if( name == "albedo_tint" )
					strToVec3( query.getColumn( column ).getString(), layer.tint );
				else if( name == "displacement" )
					layer.displacement = static_cast<float>( query.getColumn( column ).getDouble() );
				else if( name == "occlusion" )
					layer.occlusion = static_cast<float>( query.getColumn( column ).getDouble() );
			}
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
