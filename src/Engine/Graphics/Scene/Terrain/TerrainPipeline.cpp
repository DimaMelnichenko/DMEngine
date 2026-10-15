#include "TerrainPipeline.h"
#include "TerrainEdits.h"
#include "TerrainErosion.h"
#include "D3D\TextureImages.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <DirectXTex.h>
#include <DirectXPackedVector.h>
#include "System.h"
#include "DBConnector.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "Utils\Fingerprint.h"
#include "Properties\SettingsProperties.h"
#include "ObjectLibrary\SettingsDatabase.h"

using namespace DirectX;

namespace
{

// Настройки рельефной части поля ветра (поток и подветренная тень, TerrainWind::solveTerrain); лес — остальные. Их правка
// меняет и снег в правилах слоёв
using WindSetting = float GS::WindFieldSettings::*;
constexpr WindSetting windTerrainSettings[] = { &GS::WindFieldSettings::layerHeight, &GS::WindFieldSettings::layerSmoothing,
												&GS::WindFieldSettings::maxSpeedup, &GS::WindFieldSettings::reach,
												&GS::WindFieldSettings::shelterFrom, &GS::WindFieldSettings::shelterTo,
												&GS::WindFieldSettings::leeSlowdown, &GS::WindFieldSettings::turbulence };

bool loadFloatDDS( const std::string& path, uint32_t size, std::vector<float>& values )
{
	ScratchImage image;
	const std::wstring wide( path.begin(), path.end() );
	if( FAILED( LoadFromDDSFile( wide.c_str(), DDS_FLAGS_NONE, nullptr, image ) ) || image.GetMetadata().format != DXGI_FORMAT_R32_FLOAT ||
		image.GetMetadata().width != size || image.GetMetadata().height != size )
		return false;
	const Image* level = image.GetImage( 0, 0, 0 );
	values.resize( static_cast<size_t>( size ) * size );
	for( uint32_t row = 0; row < size; ++row )
		memcpy( &values[static_cast<size_t>( row ) * size], level->pixels + row * level->rowPitch, size * sizeof( float ) );
	return true;
}

bool saveFloatDDS( const std::string& path, uint32_t size, const std::vector<float>& values )
{
	return GpuImages::saveFloatDDS( std::wstring( path.begin(), path.end() ), size, size, values.data() );
}

// Высота земли в точке рельефа, м (строки сверху вниз; z = W − (строка + 0,5) · шаг), билинейно
float sampleMetres( const std::vector<float>& heights, uint32_t size, float cell, float x, float z )
{
	const float col = std::clamp( x / cell - 0.5f, 0.0f, size - 1.001f );
	const float row = std::clamp( ( size * cell - z ) / cell - 0.5f, 0.0f, size - 1.001f );
	const uint32_t c = static_cast<uint32_t>( col );
	const uint32_t r = static_cast<uint32_t>( row );
	const float fc = col - c;
	const float fr = row - r;
	auto at = [&]( uint32_t rr, uint32_t cc ) { return heights[static_cast<size_t>( rr ) * size + cc]; };
	const float top = at( r, c ) + ( at( r, c + 1 ) - at( r, c ) ) * fc;
	const float bottom = at( r + 1, c ) + ( at( r + 1, c + 1 ) - at( r + 1, c ) ) * fc;
	return top + ( bottom - top ) * fr;
}

// Самая высокая точка земли под моделью — в круге radius вокруг (x, z), по сетке 5 × 5 точек (как прежний gen_heightmap.py)
float footprintTop( const std::vector<float>& heights, uint32_t size, float cell, float x, float z, float radius )
{
	float top = -FLT_MAX;
	for( int i = 0; i < 5; ++i )
	{
		for( int j = 0; j < 5; ++j )
		{
			const float ox = radius * ( i / 2.0f - 1.0f );
			const float oz = radius * ( j / 2.0f - 1.0f );
			if( ox * ox + oz * oz <= radius * radius + 1e-6f )
				top = std::max( top, sampleMetres( heights, size, cell, x + ox, z + oz ) );
		}
	}
	return top;
}

}

namespace GS
{

TerrainPipeline::TerrainPipeline() :
	SceneObject( "Terrain pipeline" )
{
}

bool TerrainPipeline::initialize( uint32_t terrainId, TerrainSettings& settings, const std::vector<TerrainEdit>& edits,
								  const WaterSimulationSettings* water, const TerrainErosionSettings* erosion, const TerrainLayerRules& layerRules )
{
	if( erosion )
		m_erosion = *erosion;
	m_layerRules = layerRules;
	if( !loadSettings( terrainId, settings ) )
		return false;
	m_terrainId = terrainId;
	m_heightMultiplier = settings.heightMultiplier;

	const uint32_t mapSize = System::textures().get( m_heightMapName )->width();
	m_worldSize = mapSize * m_texelSize;

	// Доли — по времени загрузки (Release, Test): конвейер рельефа и воды с правилами слоёв ~0,7 с, слои материала
	// (фото-текстуры с мипами) ~1,5 с
	SlowTask task( 1.0f );
	task.enterProgressFrame( 0.3f );
	TerrainEditCoverage coverage;
	if( !buildHeights( edits, water, coverage ) || !createFoliageClearMask( coverage ) )
		return false;
	m_coverage = coverage;

	task.enterProgressFrame( 0.7f, "Material layers" );
	if( !m_material.initialize( terrainId, {} ) || !repaintSplat() || !buildDetailSplat() )
		return false;
	for( uint32_t layer = m_material.layerCount(); layer < TerrainEditCoverage::paintLayers; ++layer )
		if( !coverage.paint[layer].empty() )
			LOG( "Terrain: terrain edits paint layer " + std::to_string( layer ) + ", which TerrainLayers does not describe" );

	addLayerProperties();
	if( m_erosion )
		addErosionProperties( *m_erosion );
	addLayerRulesProperties( m_layerRules );
	addPaintProperties();
	m_initialized = true;
	return true;
}

void TerrainPipeline::attachWindow( PropertyContainer& window )
{
	m_window = &window;
	window.addSubContainer( &m_layerProperties );
	window.addSubContainer( &m_layerTintProperties );
	window.addSubContainer( &m_layerDisplacementProperties );
	window.addSubContainer( &m_layerOcclusionProperties );
	if( m_erosion )
		window.addSubContainer( &m_erosionProperties );
	window.addSubContainer( &m_layerRulesProperties );
	window.addSubContainer( &m_paintProperties );
}

void TerrainPipeline::addLayerProperties()
{
	// Метров на повтор текстуры слоя (TerrainLayers.tiling)
	Property* prop = nullptr;
	m_layerProperties.setName( "Layer tiling" );
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
	{
		if( m_material.layerName( layer ).empty() )
			continue;
		prop = m_layerProperties.insert( std::to_string( layer ) + ": " + m_material.layerName( layer ), m_material.tiling( layer ) );
		prop->setLow( 0.25f );
		prop->setHigh( std::max( 64.0f, m_material.tiling( layer ) * 2.0f ) );
		prop->setLogarithmic()->setUnit( "m" )->setTooltip( "Metres per texture repeat" );
	}
	// Множитель альбедо слоя (TerrainLayers.albedo_tint): 1 — текстура как есть, больше — светлее
	m_layerTintProperties.setName( "Layer tint" );
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
	{
		if( m_material.layerName( layer ).empty() )
			continue;
		prop = m_layerTintProperties.insert( std::to_string( layer ) + ": " + m_material.layerName( layer ), m_material.tint( layer ) );
		prop->setLow( 0.0f );
		prop->setHigh( 4.0f );
		prop->setTooltip( "Albedo multiplier of the layer, R G B (Layer Tint in UE Landscape materials): 1 - the texture as is" );
	}
	// Размах дисплейсмента слоя (TerrainLayers.displacement): высота текстуры сдвигает землю на ±половину
	m_layerDisplacementProperties.setName( "Layer displacement" );
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
	{
		if( m_material.layerName( layer ).empty() )
			continue;
		prop = m_layerDisplacementProperties.insert( std::to_string( layer ) + ": " + m_material.layerName( layer ),
													 m_material.displacement( layer ) );
		prop->setLow( 0.0f );
		prop->setHigh( 0.5f );
		prop->setUnit( "m" )->setTooltip( "Displacement range of the layer: its height map moves the ground by +-half of it "
										  "along the normal near the camera (0 - flat)" );
	}
	// Сила затенения окружением слоя (TerrainLayers.occlusion): AO из B массива нормалей — pack_terrain_layer.py
	m_layerOcclusionProperties.setName( "Layer occlusion" );
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
	{
		if( m_material.layerName( layer ).empty() )
			continue;
		prop = m_layerOcclusionProperties.insert( std::to_string( layer ) + ": " + m_material.layerName( layer ),
												  m_material.occlusion( layer ) );
		prop->setLow( 0.0f );
		prop->setHigh( 1.0f );
		prop->setControlType( GUIControlType::SLIDER );
		prop->setTooltip( "Ambient occlusion of the layer texture: 1 - as baked, 0 - none. Darkens the sky light in the crevices "
						  "and, with Micro shadows, the sun" );
	}
}

TerrainHeight TerrainPipeline::terrainHeight() const
{
	TerrainHeight height;
	height.heightMap = &m_heightMap;
	height.foliageClearMask = &m_foliageClearMask;
	height.worldSize = m_worldSize;
	height.mapSize = mapSize();
	height.heightMultiplier = m_heightMultiplier;
	height.heightOffset = m_heightOffset;
	height.maxValue = m_maxHeightValue;
	height.revision = m_heightRevision;
	height.detailTileSize = m_detailTileSize;
	height.detailTiles = &m_detailView;
	height.detailIndex = &m_detailIndexView;
	return height;
}

const XMFLOAT2* TerrainPipeline::detailBounds( uint32_t x, uint32_t z ) const
{
	if( m_detailTileSize <= 0.0f || x >= m_detailTilesPerSide || z >= m_detailTilesPerSide )
		return nullptr;
	const int slice = m_detailIndex[static_cast<size_t>( z ) * m_detailTilesPerSide + x];
	return slice >= 0 ? &m_detailBounds[slice] : nullptr;
}

int32_t TerrainPipeline::paintPreview( const ShaderView*& mask ) const
{
	// Цель кисти поверх земли: слой, мокрость или маска (её текстура хранилища — t7 пиксельного шейдера рельефа)
	mask = nullptr;
	if( !m_paintPreview || !m_paintProperties.exists( "Show target" ) || !m_paintProperties["Show target"].data<bool>() )
		return -1;
	const std::string target = paintBrush( false, false ).target;
	const int32_t layer = TerrainPaint::layerOf( target );
	if( layer >= 0 && static_cast<uint32_t>( layer ) < TerrainMaterial::maxLayers )
		return layer;
	if( target == TerrainPaint::wetnessTarget )
		return static_cast<int32_t>( TerrainMaterial::maxLayers );
	if( !target.empty() && System::textures().exists( target ) )
	{
		mask = &System::textures().get( System::textures().id( target ) )->srv();
		return static_cast<int32_t>( TerrainMaterial::maxLayers ) + 1;
	}
	return -1;
}

void TerrainPipeline::update( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
	{
		const std::string name = std::to_string( layer ) + ": " + m_material.layerName( layer );
		if( m_layerProperties.exists( name ) )
			m_material.setTiling( layer, std::max( m_layerProperties[name].data<float>(), 0.01f ) );
		if( m_layerTintProperties.exists( name ) )
		{
			const XMFLOAT3 tint = m_layerTintProperties[name].data<XMFLOAT3>();
			m_material.setTint( layer, XMFLOAT3( std::max( tint.x, 0.0f ), std::max( tint.y, 0.0f ), std::max( tint.z, 0.0f ) ) );
		}
		if( m_layerDisplacementProperties.exists( name ) )
			m_material.setDisplacement( layer, m_layerDisplacementProperties[name].data<float>() );
		if( m_layerOcclusionProperties.exists( name ) )
			m_material.setOcclusion( layer, m_layerOcclusionProperties[name].data<float>() );
	}
	// Мазок тонкой сетки — плитки детального слоя весов раз в кадр, splat-карта — не чаще 10 раз в секунду
	flushPaint( false );
}

void TerrainPipeline::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	DMD3D::instance().setSRV( SLOT_TERRAIN_DETAIL, m_detailView );
	DMD3D::instance().setSRV( SLOT_TERRAIN_DETAIL_INDEX, m_detailIndexView );
	if( m_windView.valid() )
		DMD3D::instance().setSRV( SLOT_WIND_FIELD, m_windView );
	// Слои материала — ресурсы сцены (terrain_layers.sh): выбор слоёв и дисплейсмент у шейдеров террейна, расстановки и
	// частиц — по одним данным
	m_material.bindScene();
	m_detailSplat.bind();
}

void TerrainPipeline::settings( TerrainSettings& settings ) const
{
	settings.paint = m_paint.initialized() ? m_paint.tiles() : m_paintTiles;
	settings.layerTiling.assign( m_material.layerCount(), 0.0f );
	settings.layerTint.assign( m_material.layerCount(), XMFLOAT3( 1.0f, 1.0f, 1.0f ) );
	settings.layerDisplacement.assign( m_material.layerCount(), 0.0f );
	settings.layerOcclusion.assign( m_material.layerCount(), 1.0f );
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
		if( !m_material.layerName( layer ).empty() )
		{
			settings.layerTiling[layer] = m_material.tiling( layer );
			settings.layerTint[layer] = m_material.tint( layer );
			settings.layerDisplacement[layer] = m_material.displacement( layer );
			settings.layerOcclusion[layer] = m_material.occlusion( layer );
		}
	if( m_erosion )
		readSettingProperties( m_erosionProperties, terrainErosionSchema(), settings.erosion.emplace( *m_erosion ) );
	settings.layerRules = layerRulesFromProperties();
}

bool TerrainPipeline::loadEditRaster( TerrainEdit& edit )
{
	// Карта сдвига высоты, м: R32_FLOAT квадратом, путь — от Textures\ (как у текстур хранилища)
	const std::wstring path = L"Textures\\" + std::wstring( edit.raster.begin(), edit.raster.end() );
	ScratchImage image;
	if( FAILED( LoadFromDDSFile( path.c_str(), DDS_FLAGS_NONE, nullptr, image ) ) || image.GetImageCount() == 0 ||
		image.GetMetadata().format != DXGI_FORMAT_R32_FLOAT || image.GetMetadata().width != image.GetMetadata().height )
	{
		LOG( "Terrain: raster of terrain edit " + edit.name + " (" + edit.raster + ") is not loaded: square R32_FLOAT DDS expected" );
		return false;
	}
	const Image* level = image.GetImage( 0, 0, 0 );
	edit.rasterSize = static_cast<uint32_t>( level->width );
	edit.rasterValues.resize( static_cast<size_t>( edit.rasterSize ) * edit.rasterSize );
	for( uint32_t row = 0; row < edit.rasterSize; ++row )
		memcpy( edit.rasterValues.data() + static_cast<size_t>( row ) * edit.rasterSize, level->pixels + row * level->rowPitch,
				edit.rasterSize * sizeof( float ) );
	return true;
}

bool TerrainPipeline::loadSettings( uint32_t terrainId, TerrainSettings& settings )
{
	// Колонка splatmap больше не читается: splat-карту дают правила по итоговому рельефу (TerrainMasks)
	SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM Terrain WHERE id = :id" );
	query.bind( ":id", terrainId );

	if( !query.executeStep() )
	{
		LOG( "Terrain: no row " + std::to_string( terrainId ) + " in table Terrain" );
		return false;
	}

	m_heightMapName = query.getColumn( "heightmap" ).getString();
	settings.id = terrainId;
	// Материал и сетка — по описанию полей (terrainSchema): NULL — значения по умолчанию TerrainSettings
	readSettings( query, terrainSchema(), settings, "Terrain " + std::to_string( terrainId ) );
	m_heightOffset = static_cast<float>( query.getColumn( "height_offset" ).getDouble() );
	m_texelSize = static_cast<float>( query.getColumn( "width_multiplier" ).getDouble() );

	// Кэш и карты эрозии — рядом с файлом карты высот: Textures\<его каталог>\eroded
	SQLite::Statement file( DBConnector::instance().db(), "select file from Textures where name = :name" );
	file.bind( ":name", m_heightMapName );
	std::string directory;
	if( file.executeStep() )
	{
		const std::string path = file.getColumn( "file" ).getString();
		const size_t slash = path.find_last_of( "\\/" );
		directory = slash == std::string::npos ? std::string() : path.substr( 0, slash );
	}
	m_erodedDirectory = "Textures\\" + ( directory.empty() ? std::string() : directory + "\\" ) + "eroded";
	m_generatedDirectory = "Textures\\" + ( directory.empty() ? std::string() : directory + "\\" ) + "generated";

	// Роли слоёв (TerrainLayers.role): какой слой красят скала, снег, осыпи и травы правил слоёв и галька русел
	m_layerOf.fill( -1 );
	if( DBConnector::instance().db().tableExists( "TerrainLayers" ) )
	{
		SQLite::Statement roles( DBConnector::instance().db(), "select layer, role from TerrainLayers where terrain = :terrain and role is not null" );
		roles.bind( ":terrain", terrainId );
		while( roles.executeStep() )
		{
			const std::string name = roles.getColumn( "role" ).getString();
			const TerrainLayerRole role = terrainLayerRole( name );
			if( role == TerrainLayerRole::count )
			{
				LOG( "Terrain: unknown layer role " + name + " in TerrainLayers" );
				continue;
			}
			m_layerOf[static_cast<uint32_t>( role )] = roles.getColumn( "layer" ).getInt();
		}
	}
	for( uint32_t role = 0; role < terrainRuleRoleCount; ++role )
		if( m_layerOf[role] < 0 )
			LOG( std::string( "Terrain: no layer with role " ) + terrainLayerRoleName( static_cast<TerrainLayerRole>( role ) ) +
				 " in TerrainLayers, its weight goes to grass_dry" );
	m_channelLayer = m_layerOf[static_cast<uint32_t>( TerrainLayerRole::channelBed )];
	if( m_channelLayer < 0 )
		LOG( "Terrain: no layer with role channel_bed in TerrainLayers, channel beds are not painted" );
	return true;
}

bool TerrainPipeline::buildHeights( const std::vector<TerrainEdit>& edits, const WaterSimulationSettings* water, TerrainEditCoverage& coverage )
{
	// Почти всё — русла и озёра (finishHeights); эрозия из кэша — чтение файла, заново — секунды на большой карте
	SlowTask task( 1.0f );
	task.enterProgressFrame( 0.05f, "Height map" );
	ScratchImage captured;
	if( !GpuImages::captureTexture( System::textures().get( m_heightMapName )->texture(), captured ) )
	{
		LOG( "Terrain: can`t copy heightmap to CPU" );
		return false;
	}

	const Image* image = captured.GetImage( 0, 0, 0 );

	ScratchImage decompressed;
	if( IsCompressed( image->format ) )
	{
		if( FAILED( Decompress( *image, DXGI_FORMAT_UNKNOWN, decompressed ) ) )
		{
			LOG( "Terrain: can`t decompress heightmap" );
			return false;
		}
		image = decompressed.GetImage( 0, 0, 0 );
	}

	ScratchImage converted;
	if( image->format == DXGI_FORMAT_R16_UNORM )
	{
		// R16 — сами, точным делением: Convert DirectXTex округляет в Debug и Release по-разному (SIMD), а от исходной карты
		// зависит кэш эрозии и её результат
		if( FAILED( converted.Initialize2D( DXGI_FORMAT_R32_FLOAT, image->width, image->height, 1, 1 ) ) )
		{
			LOG( "Terrain: can`t convert heightmap to R32_FLOAT" );
			return false;
		}
		const Image* target = converted.GetImage( 0, 0, 0 );
		for( size_t row = 0; row < image->height; ++row )
		{
			const uint16_t* from = reinterpret_cast<const uint16_t*>( image->pixels + row * image->rowPitch );
			float* to = reinterpret_cast<float*>( target->pixels + row * target->rowPitch );
			for( size_t col = 0; col < image->width; ++col )
				to[col] = static_cast<float>( from[col] ) / 65535.0f;
		}
		image = target;
	}
	else if( image->format != DXGI_FORMAT_R32_FLOAT )
	{
		if( FAILED( Convert( *image, DXGI_FORMAT_R32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted ) ) )
		{
			LOG( "Terrain: can`t convert heightmap to R32_FLOAT" );
			return false;
		}
		image = converted.GetImage( 0, 0, 0 );
	}

	// Конвейер рельефа и воды на копии до мипов (её видят вершины всех уровней, узлы, расстановка и вода): ручные правки
	// (TerrainEdits) → русла по итоговому рельефу (TerrainHydrology) — каждая ступень видит итог предыдущих
	HeightField field;
	field.heights = reinterpret_cast<float*>( image->pixels );
	field.size = static_cast<uint32_t>( image->width );
	field.rowPitch = image->rowPitch / sizeof( float );
	field.texelSize = m_texelSize;
	field.heightMultiplier = m_heightMultiplier;
	field.heightOffset = m_heightOffset;
	// Первая ступень — эрозия исходной карты (до правок: правки — поверх размытого рельефа)
	task.enterProgressFrame( 0.1f, "Erosion" );
	if( m_erosion && !erode( field, *m_erosion ) )
		return false;
	task.enterProgressFrame( 0.02f, "Terrain edits" );
	if( !edits.empty() )
	{
		std::vector<TerrainEdit> loaded = edits;
		for( TerrainEdit& edit : loaded )
			if( !edit.raster.empty() )
				loadEditRaster( edit );
		const size_t changed = applyTerrainEdits( field, loaded, &coverage );
		LOG( "Terrain: " + std::to_string( edits.size() ) + " terrain edits, texels changed: " + std::to_string( changed ) );
	}
	// Основа для перестройки русел без перезагрузки (rebuildChannels): рельеф после эрозии и ручных правок и их покрытие
	m_baseSize = field.size;
	m_baseHeights.resize( static_cast<size_t>( field.size ) * field.size );
	for( uint32_t row = 0; row < field.size; ++row )
		std::memcpy( &m_baseHeights[static_cast<size_t>( row ) * field.size], field.heights + row * field.rowPitch, field.size * sizeof( float ) );
	m_baseCoverage = coverage;

	m_hasHydrology = false;
	task.enterProgressFrame( 0.03f, "Stream curves" );
	if( water )
	{
		// Кривые русел — из базы; рельеф или параметры генератора сменились (отпечаток не совпал) — генерация заново:
		// прежние несправленные заменяются, правленные и ручные остаются (Scene пишет новые в базу)
		m_streamCurves = water->streams;
		m_streamsKey = TerrainHydrology::generationKey( field, *water );
		m_streamsRegenerated = m_streamsKey != water->streamsKey;
		if( m_streamsRegenerated )
		{
			std::vector<StreamCurve> generated;
			if( !TerrainHydrology::generate( field, *water, generated ) )
			{
				LOG( "Terrain: stream curves are not generated" );
				return false;
			}
			m_streamCurves = TerrainHydrology::merge( generated, water->streams, water->channels );
			size_t kept = 0;
			for( const StreamCurve& curve : m_streamCurves )
				kept += curve.generated && !curve.edited ? 0 : 1;
			LOG( "Terrain hydrology: stream curves generated: " + std::to_string( m_streamCurves.size() - kept ) + ", edited and manual kept: " +
				 std::to_string( kept ) );
		}
	}
	task.enterProgressFrame( 0.8f );
	return finishHeights( *image, water, coverage );
}

bool TerrainPipeline::finishHeights( const Image& image, const WaterSimulationSettings* water, TerrainEditCoverage& coverage )
{
	HeightField field;
	field.heights = reinterpret_cast<float*>( image.pixels );
	field.size = static_cast<uint32_t>( image.width );
	field.rowPitch = image.rowPitch / sizeof( float );
	field.texelSize = m_texelSize;
	field.heightMultiplier = m_heightMultiplier;
	field.heightOffset = m_heightOffset;
	m_hasHydrology = false;
	// Те же шаги и при перестройке русел в кадре — там у SlowTask нет обработчика, и он ничего не делает
	SlowTask task( 1.0f );
	task.enterProgressFrame( 0.6f, "Rivers and lakes" );
	if( water )
	{
		const auto start = std::chrono::steady_clock::now();
		if( !TerrainHydrology::build( field, *water, m_streamCurves, m_hydrology ) )
		{
			LOG( "Terrain: water channels are not built" );
			return false;
		}
		applyTerrainEdits( field, TerrainHydrology::loweringEdits( m_hydrology, water->staticWater, m_channelLayer ), &coverage );
		m_staticWater = water->staticWater;
		m_hasHydrology = true;
		for( const std::string& warning : m_hydrology.warnings )
			LOG( "Terrain hydrology: " + warning );
		char text[256];
		std::snprintf( text, sizeof( text ), "Terrain hydrology: largest discharge %.3f m3/s, channel nodes %zu, carved cells %zu, "
					   "deepest %.2f m, streams %zu, points %zu, stones %zu, ms: %.1f", m_hydrology.largestDischarge, m_hydrology.nodes,
					   m_hydrology.carvedCells, -m_hydrology.deepestLowering, m_hydrology.streamCount, m_hydrology.points,
					   m_hydrology.stones.size(),
					   std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
		LOG( text );
	}
	task.enterProgressFrame( 0.1f, "Detail ground" );
	if( !createDetailTiles() )
		return false;
	// Слои и маски — по итоговому рельефу и руслам: последняя ступень конвейера до воды
	task.enterProgressFrame( 0.3f, "Layers and masks" );
	if( !buildMasks( field ) )
		return false;

	// Копия итоговой карты на CPU — высота поверхности для ходьбы (surfaceHeight); 1024² — 4 МБ
	m_cpuSize = static_cast<uint32_t>( image.width );
	m_cpuHeights.resize( static_cast<size_t>( m_cpuSize ) * image.height );
	for( size_t row = 0; row < image.height; ++row )
		std::memcpy( &m_cpuHeights[row * m_cpuSize], image.pixels + row * image.rowPitch, m_cpuSize * sizeof( float ) );
	computeWaterLevel();

	// Вершины уровня L читают мипы L и L + 1, поэтому вершинному шейдеру нужна карта высот с полной цепочкой мипов
	ScratchImage mipChain;
	if( FAILED( GenerateMipMaps( image, TEX_FILTER_DEFAULT, 0, mipChain ) ) )
	{
		LOG( "Terrain: can`t generate heightmap mips" );
		return false;
	}

	if( !GpuImages::createTexture( mipChain, m_heightMapTexture, m_heightMap ) )
	{
		LOG( "Terrain: can`t create heightmap texture" );
		return false;
	}
	DMD3D::instance().setName( m_heightMapTexture, "Terrain height map" );
	// Мипы на CPU — те же значения, что читают вершины: по ним границы узлов рельефа (CDLODTerrain)
	m_heightMips.assign( mipChain.GetMetadata().mipLevels, {} );
	for( size_t mip = 0; mip < m_heightMips.size(); ++mip )
	{
		const Image* mipImage = mipChain.GetImage( mip, 0, 0 );
		HeightMip& target = m_heightMips[mip];
		target.size = static_cast<uint32_t>( mipImage->width );
		target.values.resize( static_cast<size_t>( mipImage->width ) * mipImage->height );
		for( size_t row = 0; row < mipImage->height; ++row )
			std::memcpy( &target.values[row * mipImage->width], mipImage->pixels + row * mipImage->rowPitch, mipImage->width * sizeof( float ) );
	}
	m_maxHeightValue = m_cpuHeights.empty() ? 0.0f : *std::max_element( m_cpuHeights.begin(), m_cpuHeights.end() );
	++m_heightRevision;
	return true;
}

bool TerrainPipeline::rebuildChannels( const WaterSimulationSettings& water, const std::vector<StreamCurve>& curves )
{
	if( m_baseHeights.empty() )
		return false;
	// Рельеф после эрозии и правок — заново, русла по нынешним кривым поверх
	ScratchImage image;
	if( FAILED( image.Initialize2D( DXGI_FORMAT_R32_FLOAT, m_baseSize, m_baseSize, 1, 1 ) ) )
		return false;
	const Image& target = *image.GetImage( 0, 0, 0 );
	for( uint32_t row = 0; row < m_baseSize; ++row )
		std::memcpy( target.pixels + row * target.rowPitch, &m_baseHeights[static_cast<size_t>( row ) * m_baseSize], m_baseSize * sizeof( float ) );
	TerrainEditCoverage coverage = m_baseCoverage;
	m_streamCurves = curves;
	if( !finishHeights( target, &water, coverage ) )
		return false;
	m_coverage = coverage;
	if( !createFoliageClearMask( coverage ) || !repaintSplat() || !buildDetailSplat() )
		return false;
	// Рельеф у русел и лес сменились — поле ветра заново
	if( !m_wind.empty() )
		rebuildWindField();
	return true;
}

bool TerrainPipeline::layerRulesChanged() const
{
	if( !m_initialized )
		return false;
	return !settingsEqual( terrainLayerRulesSchema(), layerRulesFromProperties(), m_layerRules );
}

void TerrainPipeline::setPrevailingWind( const XMFLOAT3& direction )
{
	m_prevailingWind = XMFLOAT2( direction.x, direction.z );
}

bool TerrainPipeline::prevailingWindChanged( const XMFLOAT3& direction ) const
{
	return direction.x != m_prevailingWind.x || direction.z != m_prevailingWind.y;
}

void TerrainPipeline::setWindField( const WindFieldSettings& settings )
{
	m_windSettings = settings;
}

bool TerrainPipeline::windFieldChanged( const WindFieldSettings& settings ) const
{
	return !settingsEqual( windFieldSchema(), settings, m_windSettings );
}

bool TerrainPipeline::windTerrainChanged( const WindFieldSettings& settings ) const
{
	for( float WindFieldSettings::* setting : windTerrainSettings )
		if( settings.*setting != m_windSettings.*setting )
			return true;
	return false;
}

bool TerrainPipeline::buildWindField( const std::vector<WindObstacle>& obstacles )
{
	m_windObstacles = obstacles;
	return rebuildWindField();
}

uint64_t TerrainPipeline::windTerrainKey( const HeightField& field, TerrainField& metres ) const
{
	metres.resize( static_cast<size_t>( field.size ) * field.size );
	for( uint32_t row = 0; row < field.size; ++row )
		for( uint32_t col = 0; col < field.size; ++col )
			metres[static_cast<size_t>( row ) * field.size + col] = field.heights[static_cast<size_t>( row ) * field.rowPitch + col] * field.heightMultiplier;
	Fingerprint key;
	const char* stamp = TerrainWind::codeStamp();
	key.add( stamp, std::strlen( stamp ) );
	key.add( field.size );
	key.add( field.texelSize );
	key.addWords( metres.data(), metres.size() * sizeof( float ) );
	key.add( m_prevailingWind );
	for( float WindFieldSettings::* setting : windTerrainSettings )
		key.add( m_windSettings.*setting );
	return key.value;
}

bool TerrainPipeline::ensureWindTerrain( const HeightField& field )
{
	TerrainField metres;
	const uint64_t key = windTerrainKey( field, metres );
	if( key == m_windTerrainKey && !m_windTerrain.empty() )
		return true;
	if( !TerrainWind::solveTerrain( metres, field.size, field.texelSize, m_prevailingWind.x, m_prevailingWind.y, m_windSettings, m_windTerrain ) )
		return false;
	m_windTerrainKey = key;
	return true;
}

bool TerrainPipeline::rebuildWindField()
{
	if( m_cpuHeights.empty() )
		return false;
	const auto start = std::chrono::steady_clock::now();
	if( std::hypot( m_prevailingWind.x, m_prevailingWind.y ) <= 1e-3f )
	{
		// Без ветра у уровня — поля нет: шейдеры берут ровный ветер уровня
		m_wind.clear();
		m_windTexture = Texture();
		m_windView = ShaderView();
		return true;
	}
	const uint32_t size = m_cpuSize;
	// Рельефная часть — по итоговому рельефу (то же, что видели правила слоёв: ключ совпадёт, расчёт — из памяти)
	HeightField finalField;
	finalField.heights = m_cpuHeights.data();
	finalField.size = m_cpuSize;
	finalField.rowPitch = m_cpuSize;
	finalField.texelSize = m_texelSize;
	finalField.heightMultiplier = m_heightMultiplier;
	finalField.heightOffset = m_heightOffset;
	TerrainField heights;
	const uint64_t terrainKeyValue = windTerrainKey( finalField, heights );

	// Полог: маски леса и модели уровня; итог — в кэш на диске рядом с масками (generated\wind.dds, ключ — wind_key.txt)
	std::vector<const TerrainField*> forest;
	for( const TerrainMasks::Mask& mask : paintedMasks().masks )
		if( mask.name.rfind( "mask_forest_", 0 ) == 0 )
			forest.push_back( &mask.values );
	const TerrainField canopy = TerrainWind::canopy( forest, size, m_worldSize, m_windObstacles );
	Fingerprint key;
	key.add( terrainKeyValue );
	key.addWords( canopy.data(), canopy.size() * sizeof( float ) );
	forEachSettingValue( windFieldSchema(), m_windSettings, [&key]( const auto& value ) { key.add( value ); } );
	char keyText[32];
	std::snprintf( keyText, sizeof( keyText ), "%016llx", static_cast<unsigned long long>( key.value ) );
	const std::string path = m_generatedDirectory + "\\wind.dds";
	const std::string keyPath = m_generatedDirectory + "\\wind_key.txt";
	std::string cachedKey;
	{
		std::ifstream keyFile( keyPath );
		std::getline( keyFile, cachedKey );
	}
	std::vector<DirectX::PackedVector::HALF> halves;
	bool cached = false;
	if( cachedKey == keyText )
	{
		const std::wstring wide( path.begin(), path.end() );
		ScratchImage image;
		if( SUCCEEDED( LoadFromDDSFile( wide.c_str(), DDS_FLAGS_NONE, nullptr, image ) ) && image.GetMetadata().format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
			image.GetMetadata().width == size && image.GetMetadata().height == size )
		{
			const Image& source = *image.GetImage( 0, 0, 0 );
			halves.resize( static_cast<size_t>( size ) * size * 4 );
			for( uint32_t row = 0; row < size; ++row )
				std::memcpy( &halves[static_cast<size_t>( row ) * size * 4], source.pixels + row * source.rowPitch, size * 4 * sizeof( uint16_t ) );
			cached = true;
		}
	}
	if( !cached )
	{
		if( !ensureWindTerrain( finalField ) )
		{
			LOG( "Terrain: wind field is not solved" );
			return false;
		}
		std::vector<TerrainWind::Cell> field;
		TerrainWind::applyCanopy( m_windTerrain, size, m_texelSize, canopy, m_windSettings, field );
		halves.resize( field.size() * 4 );
		for( size_t i = 0; i < field.size(); ++i )
		{
			halves[i * 4 + 0] = DirectX::PackedVector::XMConvertFloatToHalf( field[i].x );
			halves[i * 4 + 1] = DirectX::PackedVector::XMConvertFloatToHalf( field[i].y );
			halves[i * 4 + 2] = DirectX::PackedVector::XMConvertFloatToHalf( field[i].z );
			halves[i * 4 + 3] = DirectX::PackedVector::XMConvertFloatToHalf( field[i].w );
		}
		// Ключ — после файла, прежний убирается до него (прервали запись — промах)
		std::error_code error;
		std::filesystem::remove( keyPath, error );
		std::filesystem::create_directories( m_generatedDirectory, error );
		ScratchImage image;
		if( SUCCEEDED( image.Initialize2D( DXGI_FORMAT_R16G16B16A16_FLOAT, size, size, 1, 1 ) ) )
		{
			const Image& target = *image.GetImage( 0, 0, 0 );
			for( uint32_t row = 0; row < size; ++row )
				std::memcpy( target.pixels + row * target.rowPitch, &halves[static_cast<size_t>( row ) * size * 4], size * 4 * sizeof( uint16_t ) );
			const std::wstring wide( path.begin(), path.end() );
			if( SUCCEEDED( SaveToDDSFile( image.GetImages(), image.GetImageCount(), image.GetMetadata(), DDS_FLAGS_NONE, wide.c_str() ) ) )
				std::ofstream( keyPath ) << keyText << "\n";
		}
	}
	// Копия на CPU — те же половинные значения, что у текстуры
	m_wind.resize( static_cast<size_t>( size ) * size );
	for( size_t i = 0; i < m_wind.size(); ++i )
		m_wind[i] = TerrainWind::Cell( DirectX::PackedVector::XMConvertHalfToFloat( halves[i * 4 + 0] ), DirectX::PackedVector::XMConvertHalfToFloat( halves[i * 4 + 1] ),
									   DirectX::PackedVector::XMConvertHalfToFloat( halves[i * 4 + 2] ), DirectX::PackedVector::XMConvertHalfToFloat( halves[i * 4 + 3] ) );

	DMD3D& d3d = DMD3D::instance();
	TextureDesc desc;
	desc.width = desc.height = size;
	desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.usage = TextureUsage::shaderResource;
	TextureData data;
	data.data = halves.data();
	data.rowPitch = size * 4 * sizeof( uint16_t );
	data.slicePitch = data.rowPitch * size;
	Texture texture;
	ShaderView view;
	if( !d3d.createTexture( desc, &data, texture ) || !d3d.createShaderView( texture, {}, view ) )
	{
		LOG( "Terrain: wind field texture is not created" );
		return false;
	}
	d3d.setName( texture, "Wind field" );
	m_windTexture = std::move( texture );
	m_windView = std::move( view );
	char text[64];
	std::snprintf( text, sizeof( text ), ", ms: %.1f", std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
	LOG( std::string( "Wind field" ) + ( cached ? " (from cache)" : "" ) + ": " + TerrainWind::summary( m_wind ) + ", obstacles " +
		 std::to_string( m_windObstacles.size() ) + text );
	return true;
}

bool TerrainPipeline::rebuildMasks()
{
	if( m_cpuHeights.empty() )
		return false;
	const auto start = std::chrono::steady_clock::now();
	m_layerRules = layerRulesFromProperties();
	// Итоговая карта на CPU — размеры и масштаб; уклоны и водосбор правила берут с рельефа до русел (buildMasks)
	HeightField field;
	field.heights = m_cpuHeights.data();
	field.size = m_cpuSize;
	field.rowPitch = m_cpuSize;
	field.texelSize = m_texelSize;
	field.heightMultiplier = m_heightMultiplier;
	field.heightOffset = m_heightOffset;
	if( !buildMasks( field ) || !repaintSplat() || !buildDetailSplat() )
		return false;
	// Лес в масках сменился (и ветер мог повернуть) — поле ветра заново
	if( !m_windObstacles.empty() || !m_wind.empty() )
		rebuildWindField();
	LOG( "Terrain layers and masks rebuilt without reload, ms: " +
		 std::to_string( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() ) );
	return true;
}

bool TerrainPipeline::uploadMask( const TerrainMasks::Mask& mask, const TerrainPaint::Rect* rect )
{
	// Маски — текстуры хранилища по имени (ScatterLayers.mask, ParticleEmitters.mask): есть — пишется участок на месте
	// (DMD3D::updateTexture), нет — создаётся; виды у расстановки и частиц остаются верными
	const uint32_t size = paintedMasks().size;
	if( mask.values.size() != static_cast<size_t>( size ) * size )
		return false;
	const auto byte = []( float value ) { return static_cast<uint8_t>( std::lround( std::clamp( value, 0.0f, 1.0f ) * 255.0f ) ); };
	const TerrainPaint::Rect area = rect && !rect->empty() ? *rect : TerrainPaint::Rect{ 0, 0, size, size };
	if( System::textures().exists( mask.name ) && System::textures().get( System::textures().id( mask.name ) )->width() == size )
	{
		const uint32_t width = area.x1 - area.x0;
		std::vector<uint8_t> bytes( static_cast<size_t>( width ) * ( area.row1 - area.row0 ) );
		for( uint32_t row = area.row0; row < area.row1; ++row )
			for( uint32_t x = area.x0; x < area.x1; ++x )
				bytes[static_cast<size_t>( row - area.row0 ) * width + x - area.x0] = byte( mask.values[static_cast<size_t>( row ) * size + x] );
		TextureRegion region;
		region.x = area.x0;
		region.y = area.row0;
		region.width = width;
		region.height = area.row1 - area.row0;
		if( System::textures().updateGenerated( mask.name, region, bytes.data(), width ) )
			return true;
	}
	std::vector<uint8_t> bytes( static_cast<size_t>( size ) * size );
	for( size_t i = 0; i < bytes.size(); ++i )
		bytes[i] = byte( mask.values[i] );
	TextureDesc desc;
	desc.width = desc.height = size;
	desc.format = DXGI_FORMAT_R8_UNORM;
	TextureData data;
	data.data = bytes.data();
	data.rowPitch = size;
	if( System::textures().setGenerated( mask.name, desc, data ) )
		return true;
	LOG( "Terrain: mask " + mask.name + " is not created" );
	return false;
}

void TerrainPipeline::refreshPainted()
{
	// Маски с мазками — копия правил с поправками; слои земли и мокрость — не здесь (тонкая сетка: детальный слой весов)
	m_paintActive = false;
	for( const auto& [target, tiles] : m_paint.targets() )
		m_paintActive = m_paintActive || !TerrainPaint::isFine( target );
	if( !m_paintActive )
	{
		m_painted = TerrainMasks();
		return;
	}
	m_painted = m_masks;
	for( const auto& [target, tiles] : m_paint.targets() )
		if( !TerrainPaint::isFine( target ) )
			refreshPaintedTarget( target, m_paint.whole() );
}

void TerrainPipeline::refreshPaintedTarget( const std::string& target, const TerrainPaint::Rect& rect )
{
	const uint32_t size = m_masks.size;
	auto copyRect = [&]( const std::vector<float>& from, std::vector<float>& to )
	{
		if( from.size() != to.size() )
			return;
		for( uint32_t row = rect.row0; row < rect.row1; ++row )
			std::copy( from.begin() + static_cast<size_t>( row ) * size + rect.x0, from.begin() + static_cast<size_t>( row ) * size + rect.x1,
					   to.begin() + static_cast<size_t>( row ) * size + rect.x0 );
	};
	if( TerrainPaint::isFine( target ) )
		return;
	for( size_t i = 0; i < m_masks.masks.size(); ++i )
		if( m_masks.masks[i].name == target && i < m_painted.masks.size() )
		{
			copyRect( m_masks.masks[i].values, m_painted.masks[i].values );
			m_paint.applyMask( target, m_masks.masks[i].values, m_painted.masks[i].values, rect );
		}
}

bool TerrainPipeline::beginPaintStroke( const std::string& target, TerrainPaint::Mode mode, float value, std::string& reason )
{
	if( !m_paint.initialized() )
	{
		reason = "terrain paint: no layers and masks";
		return false;
	}
	// Значение до мазков в текселе сетки цели — для режима «Value»: у тонкой сетки — копия «до кисти» плитки детального слоя
	// весов (плитки под касанием создаются до него, paintDab), у маски — правила
	const int32_t layer = TerrainPaint::layerOf( target );
	TerrainPaint::Rule rule;
	if( TerrainPaint::isFine( target ) )
	{
		if( layer >= 0 && ( static_cast<uint32_t>( layer ) >= m_material.layerCount() || m_material.layerName( layer ).empty() ) )
		{
			reason = "terrain paint: no layer " + std::to_string( layer ) + " in TerrainLayers";
			return false;
		}
		if( m_detailSplat.tileSize() <= 0.0f )
		{
			reason = "terrain paint: no detail splat";
			return false;
		}
		const uint32_t channel = layer >= 0 ? static_cast<uint32_t>( layer ) : TerrainDetailSplat::wetnessChannel;
		rule = [this, channel]( uint32_t x, uint32_t row )
		{
			float value = 0.0f;
			m_detailSplat.sourceAt( x, row, channel, value );
			return value;
		};
	}
	else
	{
		const std::vector<float>* values = nullptr;
		for( const TerrainMasks::Mask& mask : m_masks.masks )
			if( mask.name == target )
				values = &mask.values;
		if( !values )
		{
			reason = "terrain paint: no mask " + target;
			return false;
		}
		const uint32_t size = m_masks.size;
		rule = [values, size]( uint32_t x, uint32_t row ) { return ( *values )[static_cast<size_t>( row ) * size + x]; };
	}
	m_strokeBefore = m_paint.targetTiles( target );
	m_paint.beginStroke( target, mode, value, std::move( rule ) );
	m_strokeTarget = target;
	return true;
}

void TerrainPipeline::paintDab( float x, float z, float radius, float falloff )
{
	if( !m_paint.stroking() )
		return;
	// Тонкая сетка: плитки детального слоя весов под касанием — до него (их «до кисти» — значение для «Value»); задетые —
	// на GPU раз в кадр (flushPaint из update), splat-карта — не чаще 10 раз в секунду
	if( TerrainPaint::isFine( m_strokeTarget ) )
	{
		const TerrainPaint::Rect area = m_paint.dabRect( m_strokeTarget, x, z, radius );
		if( !area.empty() )
			m_detailSplat.ensure( detailSplatSource(), m_detailSplat.tilesOf( area.x0, area.row0, area.x1, area.row1, false, false ) );
		const TerrainPaint::Rect changed = m_paint.dab( x, z, radius, falloff );
		if( changed.empty() )
			return;
		for( const TerrainDetailSplat::TileKey& tile : m_detailSplat.tilesOf( changed.x0, changed.row0, changed.x1, changed.row1, true, true ) )
			m_dirtySplatTiles.insert( tile );
		m_splatPending = true;
		return;
	}
	const TerrainPaint::Rect changed = m_paint.dab( x, z, radius, falloff );
	if( changed.empty() )
		return;
	// Первая цель с мазками — копия правил целиком, дальше — только задетый прямоугольник цели
	if( !m_paintActive || m_painted.size == 0 )
		refreshPainted();
	else
		refreshPaintedTarget( m_strokeTarget, changed );
	// Вживую: маска — сразу на GPU (её читает кольцевая трава)
	for( const TerrainMasks::Mask& mask : paintedMasks().masks )
		if( mask.name == m_strokeTarget )
			uploadMask( mask, &changed );
}

TerrainPipeline::PaintEdit TerrainPipeline::endPaintStroke()
{
	PaintEdit edit;
	if( !m_paint.stroking() )
		return edit;
	const TerrainPaint::Rect touched = m_paint.endStroke();
	edit.target = m_strokeTarget;
	edit.before = std::move( m_strokeBefore );
	edit.after = m_paint.targetTiles( m_strokeTarget );
	edit.changed = !touched.empty();
	m_strokeBefore.clear();
	// Плитки без поправок убраны — копия с мазками заново, если целей не осталось
	if( !m_paint.has( m_strokeTarget ) )
		refreshPainted();
	paintTargetChanged( m_strokeTarget, &touched );
	m_strokeTarget.clear();
	return edit;
}

void TerrainPipeline::paintTargetChanged( const std::string& target, const TerrainPaint::Rect* changed )
{
	// Мазки — данные уровня: окно террейна изменено до «Save level»
	if( m_window )
		m_window->setStructureModified( true );
	if( TerrainPaint::isFine( target ) )
	{
		// Плитки в прямоугольнике (нет — создаются: повтор мазка), остальное — массив заново; splat-карта — сейчас
		if( !changed )
			buildDetailSplat();
		else if( !changed->empty() )
		{
			const TerrainDetailSplat::Source source = detailSplatSource();
			m_detailSplat.ensure( source, m_detailSplat.tilesOf( changed->x0, changed->row0, changed->x1, changed->row1, false, false ) );
			for( const TerrainDetailSplat::TileKey& tile : m_detailSplat.tilesOf( changed->x0, changed->row0, changed->x1, changed->row1, true, true ) )
				m_dirtySplatTiles.insert( tile );
		}
		m_splatPending = true;
		flushPaint( true );
		return;
	}
	for( const TerrainMasks::Mask& mask : paintedMasks().masks )
		if( mask.name == target )
			uploadMask( mask );
	// Постоянные слои (лес) — раскладка заново (Scene), лес — ещё полог поля ветра
	m_paintedMasksChanged = true;
	if( target.rfind( "mask_forest_", 0 ) == 0 && !m_wind.empty() )
		rebuildWindField();
}

void TerrainPipeline::setPaintTiles( const std::string& target, const TerrainPaint::TileMap& tiles )
{
	// Задеты плитки прежнего и нового снимка
	TerrainPaint::Rect rect = m_paint.tilesRect( target, m_paint.targetTiles( target ) );
	rect.add( m_paint.tilesRect( target, tiles ) );
	m_paint.setTargetTiles( target, tiles );
	refreshPainted();
	paintTargetChanged( target, &rect );
}

void TerrainPipeline::flushPaint( bool force )
{
	if( !m_dirtySplatTiles.empty() )
	{
		m_detailSplat.refresh( detailSplatSource(), std::vector<TerrainDetailSplat::TileKey>( m_dirtySplatTiles.begin(), m_dirtySplatTiles.end() ) );
		m_dirtySplatTiles.clear();
	}
	const auto now = std::chrono::steady_clock::now();
	if( m_splatPending && ( force || now - m_splatUploaded > std::chrono::milliseconds( 100 ) ) )
	{
		repaintSplat();
		m_splatUploaded = now;
		m_splatPending = false;
	}
}

TerrainMaterial::SplatWeights TerrainPipeline::baseSplat() const
{
	TerrainMaterial::SplatWeights base;
	const uint32_t size = m_masks.size;
	base.size = size;
	if( size == 0 )
		return base;
	const size_t cells = static_cast<size_t>( size ) * size;
	const uint32_t layers = std::min( m_material.layerCount(), TerrainMaterial::maxLayers );
	for( uint32_t layer = 0; layer < TerrainMaterial::maxLayers; ++layer )
		base.layers[layer] = m_masks.splat[layer];
	// Покраска правками рельефа и руслами — поверх правил: вес × remaining + покраска слоя (слоёв дальше layerCount в
	// массивах нет — их покраски нет); покрытие — по центрам клеток
	if( m_coverage.paints() )
	{
		for( uint32_t layer = 0; layer < layers; ++layer )
			if( base.layers[layer].empty() && !m_coverage.paint[layer].empty() )
				base.layers[layer].assign( cells, 0.0f );
		const bool same = m_coverage.size == size;
		for( uint32_t row = 0; row < size; ++row )
			for( uint32_t col = 0; col < size; ++col )
			{
				const size_t i = static_cast<size_t>( row ) * size + col;
				const float u = ( col + 0.5f ) / size;
				const float v = ( row + 0.5f ) / size;
				const float remaining = same ? m_coverage.remaining[i] : m_coverage.sample( m_coverage.remaining, u, v );
				if( remaining >= 1.0f )
					continue;
				for( uint32_t layer = 0; layer < TerrainMaterial::maxLayers; ++layer )
				{
					if( base.layers[layer].empty() )
						continue;
					const std::vector<float>& painted = m_coverage.paint[layer];
					const float paint = layer < layers && !painted.empty() ? ( same ? painted[i] : m_coverage.sample( painted, u, v ) ) : 0.0f;
					base.layers[layer][i] = base.layers[layer][i] * remaining + paint;
				}
			}
	}
	// Мазки тонкой сетки — в среднем по клетке (вдали; вблизи — детальный слой весов)
	m_paint.applyFineAverage( base.layers.data(), layers, base.wetness, m_paint.whole() );
	return base;
}

bool TerrainPipeline::repaintSplat()
{
	return m_material.repaint( baseSplat() );
}

void TerrainPipeline::computeWaterLevel()
{
	m_waterLevel.clear();
	const uint32_t n = m_cpuSize;
	const size_t cells = static_cast<size_t>( n ) * n;
	if( !m_hasHydrology || !m_staticWater || m_hydrology.staticWater.size() != cells || m_cpuHeights.size() != cells )
		return;
	// Глубина клетки — наибольшая из озера и ручья над итоговым рельефом (mainStatic); уровень — где вода видна, у сухой
	// клетки рядом — наименьший уровень мокрых соседей (mainSurface), остальное — без уровня
	std::vector<float> ground( cells ), depth( cells );
	for( size_t i = 0; i < cells; ++i )
	{
		ground[i] = m_cpuHeights[i] * m_heightMultiplier + m_heightOffset;
		const XMFLOAT4& water = m_hydrology.staticWater[i];
		const float lake = water.w > -1e8f ? std::max( water.w - ground[i], 0.0f ) : 0.0f;
		const float stream = water.x > -1e8f ? std::max( water.x - ground[i], 0.0f ) : 0.0f;
		depth[i] = std::max( lake, stream );
	}
	m_waterLevel.assign( cells, -1e30f );
	const int32_t size = static_cast<int32_t>( n );
	for( int32_t row = 0; row < size; ++row )
		for( int32_t col = 0; col < size; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * n + col;
			if( depth[i] >= TerrainHydrology::visibleWaterDepth )
			{
				m_waterLevel[i] = ground[i] + depth[i];
				continue;
			}
			float lowest = 1e30f;
			for( int32_t y = -1; y <= 1; ++y )
				for( int32_t x = -1; x <= 1; ++x )
				{
					const int32_t r = row + y, c = col + x;
					if( r < 0 || c < 0 || r >= size || c >= size )
						continue;
					const size_t j = static_cast<size_t>( r ) * n + c;
					if( depth[j] >= TerrainHydrology::visibleWaterDepth )
						lowest = std::min( lowest, ground[j] + depth[j] );
				}
			if( lowest < 1e29f )
				m_waterLevel[i] = lowest;
		}
}

bool TerrainPipeline::paintStroke( const std::string& target, TerrainPaint::Mode mode, float value, float x, float z, float radius,
								float falloff, std::string& reason )
{
	if( !beginPaintStroke( target, mode, value, reason ) )
		return false;
	paintDab( x, z, radius, falloff );
	if( !endPaintStroke().changed )
	{
		reason = "terrain paint: the brush is outside the map";
		return false;
	}
	return true;
}

std::vector<std::string> TerrainPipeline::paintTargets() const
{
	// Слои — по номерам TerrainLayers, мокрость, затем маски (без отладочной открытости ветру)
	std::vector<std::string> targets;
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
		if( !m_material.layerName( layer ).empty() )
			targets.push_back( TerrainPaint::layerTarget( layer ) );
	targets.push_back( TerrainPaint::wetnessTarget );
	for( const TerrainMasks::Mask& mask : m_masks.masks )
		if( mask.name != "mask_wind_exposure" )
			targets.push_back( mask.name );
	return targets;
}

TerrainPipeline::PaintBrush TerrainPipeline::paintBrush( bool shift, bool ctrl ) const
{
	PaintBrush brush;
	const PropertyContainer& p = m_paintProperties;
	if( !p.exists( "Target" ) )
		return brush;
	const std::vector<std::string> targets = paintTargets();
	const int32_t target = p["Target"].data<int32_t>();
	if( target >= 0 && static_cast<size_t>( target ) < targets.size() )
		brush.target = targets[target];
	const bool step = p["Mode"].data<int32_t>() == 1;
	brush.mode = ctrl ? TerrainPaint::Mode::erase : !step ? TerrainPaint::Mode::value :
				 shift ? TerrainPaint::Mode::unstep : TerrainPaint::Mode::step;
	brush.value = p["Value"].data<float>();
	brush.radius = std::max( p["Radius"].data<float>(), 0.5f );
	brush.falloff = p["Falloff"].data<float>();
	return brush;
}

bool TerrainPipeline::raycast( const XMFLOAT3& origin, const XMFLOAT3& direction, float maxDistance, float& distance ) const
{
	// Шаг — тексель карты, над землёй / под землёй сменилось — уточнение делением пополам
	if( m_cpuHeights.empty() )
		return false;
	auto above = [&]( float t, bool& inside )
	{
		const float x = origin.x + direction.x * t;
		const float z = origin.z + direction.z * t;
		float height = 0.0f;
		inside = surfaceHeight( x, z, height );
		return origin.y + direction.y * t - height;
	};
	const float step = m_texelSize;
	bool inside = false;
	float previous = above( 0.0f, inside );
	for( float t = step; t <= maxDistance; t += step )
	{
		const float current = above( t, inside );
		if( inside && previous > 0.0f && current <= 0.0f )
		{
			float low = t - step, high = t;
			for( int i = 0; i < 16; ++i )
			{
				const float middle = 0.5f * ( low + high );
				( above( middle, inside ) > 0.0f ? low : high ) = middle;
			}
			distance = 0.5f * ( low + high );
			return true;
		}
		previous = current;
	}
	return false;
}

bool TerrainPipeline::clearPaint( const std::string& target )
{
	if( !m_paint.has( target ) )
		return false;
	const TerrainPaint::Rect rect = m_paint.tilesRect( target, m_paint.targetTiles( target ) );
	m_paint.clear( target );
	refreshPainted();
	paintTargetChanged( target, &rect );
	return true;
}

bool TerrainPipeline::paintProbe( const std::string& target, float x, float z, float& rule, float& delta, float& result ) const
{
	const uint32_t size = m_masks.size;
	if( size == 0 )
		return false;
	const int32_t column = static_cast<int32_t>( std::floor( x / m_texelSize ) );
	const int32_t row = static_cast<int32_t>( std::floor( ( size * m_texelSize - z ) / m_texelSize ) );
	if( column < 0 || row < 0 || column >= static_cast<int32_t>( size ) || row >= static_cast<int32_t>( size ) )
		return false;
	const size_t i = static_cast<size_t>( row ) * size + column;
	const int32_t layer = TerrainPaint::layerOf( target );
	const TerrainMasks& painted = paintedMasks();
	if( TerrainPaint::isFine( target ) )
	{
		// Тонкая сетка: до кисти — копия плитки детального слоя весов (нет плитки — 0), итог — с поправкой этой цели
		if( layer >= static_cast<int32_t>( TerrainMasks::splatLayers ) )
			return false;
		const float texel = m_paint.gridTexel( target );
		const uint32_t fine = m_paint.gridSize( target );
		const int32_t fineColumn = static_cast<int32_t>( std::floor( x / texel ) );
		const int32_t fineRow = static_cast<int32_t>( std::floor( ( fine * texel - z ) / texel ) );
		if( fineColumn < 0 || fineRow < 0 || fineColumn >= static_cast<int32_t>( fine ) || fineRow >= static_cast<int32_t>( fine ) )
			return false;
		rule = 0.0f;
		m_detailSplat.sourceAt( fineColumn, fineRow, layer >= 0 ? static_cast<uint32_t>( layer ) : TerrainDetailSplat::wetnessChannel, rule );
		delta = m_paint.delta( target, static_cast<uint32_t>( fineColumn ), static_cast<uint32_t>( fineRow ) );
		result = TerrainPaint::result( layer >= 0, rule, delta );
		return true;
	}
	else
	{
		size_t found = m_masks.masks.size();
		for( size_t m = 0; m < m_masks.masks.size(); ++m )
			if( m_masks.masks[m].name == target )
				found = m;
		if( found == m_masks.masks.size() )
			return false;
		rule = m_masks.masks[found].values[i];
		result = painted.masks[found].values[i];
	}
	delta = m_paint.delta( target, static_cast<uint32_t>( column ), static_cast<uint32_t>( row ) );
	return true;
}

bool TerrainPipeline::buildMasks( const HeightField& finalField )
{
	const auto start = std::chrono::steady_clock::now();
	// Уклон, высота и водосбор — по рельефу до русел (после эрозии и ручных правок, m_baseHeights): борта русел 1:1 дали бы
	// полосы скалы вдоль каждого ручья, а сток, собранный в узкие русла, — сухие луга. Русла и озёра — маской воды
	HeightField field = finalField;
	if( m_baseSize == finalField.size && !m_baseHeights.empty() )
	{
		field.heights = m_baseHeights.data();
		field.rowPitch = m_baseSize;
	}
	std::vector<float> water;
	if( m_hasHydrology && m_hydrology.size == field.size )
	{
		const size_t cells = static_cast<size_t>( field.size ) * field.size;
		water.assign( cells, 0.0f );
		for( size_t i = 0; i < cells; ++i )
			if( ( i < m_hydrology.lowering.size() && m_hydrology.lowering[i] < -0.05f ) ||
				( i < m_hydrology.staticWater.size() && m_hydrology.staticWater[i].w > -1e8f ) )
				water[i] = 1.0f;
	}

	// Кэш правил, как eroded\: отпечаток входов — рельеф до русел, карты эрозии, вода, роли слоёв, поля правил и сборка
	// TerrainMasks.cpp (правка формул меняет его сама); совпал — слои и маски из копии в generated\ (key.txt: ключ и имена
	// масок). Веса округляются до 8 бит и после расчёта (TerrainMasks::quantize) — из кэша выходит то же самое
	Fingerprint key;
	const char* stamp = TerrainMasks::codeStamp();
	key.add( stamp, std::strlen( stamp ) );
	key.add( field.size );
	key.add( field.texelSize );
	key.add( field.heightMultiplier );
	for( uint32_t row = 0; row < field.size; ++row )
		key.addWords( field.heights + static_cast<size_t>( row ) * field.rowPitch, field.size * sizeof( float ) );
	for( const std::vector<float>* map : { &m_erosionWear, &m_erosionDeposition, &m_erosionTalus, &water } )
	{
		key.add( map->size() );
		key.addWords( map->data(), map->size() * sizeof( float ) );
	}
	key.add( m_layerOf );
	key.add( m_prevailingWind );
	// Снег — по рельефной части поля ветра: её ключ (рельеф, направление, настройки поля) — в ключ масок
	const bool hasWind = std::hypot( m_prevailingWind.x, m_prevailingWind.y ) > 1e-3f;
	TerrainField windMetres;
	if( hasWind )
		key.add( windTerrainKey( finalField, windMetres ) );
	forEachSettingValue( terrainLayerRulesSchema(), m_layerRules, [&key]( const auto& value ) { key.add( value ); } );
	char keyText[32];
	std::snprintf( keyText, sizeof( keyText ), "%016llx", static_cast<unsigned long long>( key.value ) );
	const std::string keyPath = m_generatedDirectory + "\\key.txt";
	std::string cachedKey;
	std::vector<std::string> cachedNames;
	{
		std::ifstream keyFile( keyPath );
		std::getline( keyFile, cachedKey );
		for( std::string name; std::getline( keyFile, name ); )
			if( !name.empty() )
				cachedNames.push_back( name );
	}
	const bool cached = cachedKey == keyText && !cachedNames.empty() && m_masks.load( m_generatedDirectory, field.size, cachedNames );
	if( !cached )
	{
		const std::vector<float> catchment = TerrainHydrology::catchment( field );
		TerrainMaskInputs inputs;
		inputs.field = field;
		inputs.wear = &m_erosionWear;
		inputs.deposition = &m_erosionDeposition;
		inputs.talus = &m_erosionTalus;
		inputs.catchment = &catchment;
		inputs.water = water.empty() ? nullptr : &water;
		inputs.layerOf = m_layerOf;
		TerrainField windSpeed;
		if( hasWind && ensureWindTerrain( finalField ) )
		{
			windSpeed.resize( m_windTerrain.size() );
			for( size_t i = 0; i < windSpeed.size(); ++i )
				windSpeed[i] = std::hypot( m_windTerrain[i].x, m_windTerrain[i].z );
		}
		inputs.windSpeed = windSpeed.size() == static_cast<size_t>( field.size ) * field.size ? &windSpeed : nullptr;
		if( !TerrainMasks::build( inputs, m_layerRules, m_masks ) )
		{
			LOG( "Terrain: layers and masks are not built" );
			return false;
		}
		m_masks.quantize();
	}

	// Мазки кисти — поверх правил (кэш выше — только правила): при первой сборке — из базы, потом — нынешние
	if( !m_paint.initialized() || m_paint.whole().x1 != m_masks.size )
		m_paint.initialize( m_masks.size, field.texelSize, m_paintTiles );
	refreshPainted();
	for( const TerrainMasks::Mask& mask : paintedMasks().masks )
		uploadMask( mask );
	if( !cached )
	{
		// Копия на диске — и кэш: ключ пишется после файлов, а прежний убирается до них (прервали запись — промах)
		std::error_code error;
		std::filesystem::remove( keyPath, error );
		if( m_masks.save( m_generatedDirectory ) )
		{
			std::ofstream keyFile( keyPath );
			keyFile << keyText << "\n";
			for( const TerrainMasks::Mask& mask : m_masks.masks )
				keyFile << mask.name << "\n";
		}
		else
			LOG( "Terrain: can`t write layers and masks to " + m_generatedDirectory );
	}
	char text[64];
	std::snprintf( text, sizeof( text ), ", ms: %.1f", std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
	LOG( std::string( "Terrain layers and masks" ) + ( cached ? " (from cache)" : "" ) + ": " + m_masks.summary() + text );
	return true;
}

bool TerrainPipeline::createDetailTiles()
{
	const std::vector<TerrainHydrology::DetailTile> none;
	const std::vector<TerrainHydrology::DetailTile>& tiles = m_hasHydrology ? m_hydrology.detailTiles : none;
	const uint32_t tileCells = TerrainHydrology::detailTileCells;
	const uint32_t side = tileCells * TerrainHydrology::detailSamples + 2;
	const uint32_t mapSize = static_cast<uint32_t>( std::lround( m_worldSize / m_texelSize ) );
	m_detailTilesPerSide = ( mapSize + tileCells - 1 ) / tileCells;
	m_detailSide = side;
	m_detailTileSize = tiles.empty() ? 0.0f : tileCells * m_texelSize;
	m_detailIndex.assign( static_cast<size_t>( m_detailTilesPerSide ) * m_detailTilesPerSide, -1 );
	m_detailHeights.assign( tiles.size(), {} );
	m_detailBounds.assign( tiles.size(), XMFLOAT2( FLT_MAX, -FLT_MAX ) );

	// Массив: срез — плитка, нормированные высоты (как карта высот); без плиток — один нулевой срез
	const size_t slices = std::max<size_t>( tiles.size(), 1 );
	ScratchImage array;
	if( FAILED( array.Initialize2D( DXGI_FORMAT_R32G32_FLOAT, tiles.empty() ? 1 : side, tiles.empty() ? 1 : side, slices, 1 ) ) )
		return false;
	if( tiles.empty() )
		std::memset( array.GetPixels(), 0, array.GetPixelsSize() );
	for( size_t t = 0; t < tiles.size(); ++t )
	{
		const TerrainHydrology::DetailTile& tile = tiles[t];
		std::vector<float>& heights = m_detailHeights[t];
		heights.resize( tile.heights.size() );
		for( size_t i = 0; i < heights.size(); ++i )
		{
			heights[i] = ( tile.heights[i] - m_heightOffset ) / m_heightMultiplier;
			m_detailBounds[t].x = std::min( m_detailBounds[t].x, heights[i] );
			m_detailBounds[t].y = std::max( m_detailBounds[t].y, heights[i] );
		}
		const Image& target = *array.GetImage( 0, t, 0 );
		for( uint32_t row = 0; row < side; ++row )
		{
			float* line = reinterpret_cast<float*>( target.pixels + row * target.rowPitch );
			for( uint32_t col = 0; col < side; ++col )
			{
				const size_t index = static_cast<size_t>( row ) * side + col;
				line[2 * col] = heights[index];
				line[2 * col + 1] = index < tile.carve.size() ? tile.carve[index] : 0.0f;
			}
		}
		if( tile.x < m_detailTilesPerSide && tile.z < m_detailTilesPerSide )
			m_detailIndex[static_cast<size_t>( tile.z ) * m_detailTilesPerSide + tile.x] = static_cast<int>( t );
	}
	if( !GpuImages::createTexture( array, m_detailTexture, m_detailView, TextureViewDesc::Kind::texture2DArray ) )
	{
		LOG( "Terrain: can`t create detail tiles" );
		return false;
	}
	DMD3D::instance().setName( m_detailTexture, "Terrain detail tiles" );

	// Индекс: плитка → срез + 1 (0 — нет); без плиток — 1 × 1 ноль
	const uint32_t indexSide = tiles.empty() ? 1 : m_detailTilesPerSide;
	ScratchImage index;
	if( FAILED( index.Initialize2D( DXGI_FORMAT_R16_UINT, indexSide, indexSide, 1, 1 ) ) )
		return false;
	std::memset( index.GetPixels(), 0, index.GetPixelsSize() );
	if( !tiles.empty() )
	{
		const Image& target = *index.GetImage( 0, 0, 0 );
		for( uint32_t z = 0; z < indexSide; ++z )
		{
			uint16_t* line = reinterpret_cast<uint16_t*>( target.pixels + z * target.rowPitch );
			for( uint32_t x = 0; x < indexSide; ++x )
				line[x] = static_cast<uint16_t>( m_detailIndex[static_cast<size_t>( z ) * m_detailTilesPerSide + x] + 1 );
		}
	}
	if( !GpuImages::createTexture( index, m_detailIndexTexture, m_detailIndexView ) )
	{
		LOG( "Terrain: can`t create detail tile index" );
		return false;
	}
	DMD3D::instance().setName( m_detailIndexTexture, "Terrain detail tile index" );
	if( !tiles.empty() )
		LOG( "Terrain: detail tiles " + std::to_string( tiles.size() ) + " (" + std::to_string( side ) + "x" + std::to_string( side ) +
			 ", " + std::to_string( tiles.size() * side * side * 2 * sizeof( float ) / 1024 / 1024 ) + " MB)" );
	return true;
}

TerrainDetailSplat::Source TerrainPipeline::detailSplatSource() const
{
	TerrainDetailSplat::Source source;
	source.masks = &paintedMasks();
	source.texelSize = m_texelSize;
	source.edits = &m_baseCoverage;
	source.layerCount = m_material.layerCount();
	source.channelLayer = m_channelLayer < static_cast<int32_t>( m_material.layerCount() ) ? m_channelLayer : -1;
	source.gravelLayer = roleLayer( TerrainLayerRole::lakeGravel );
	source.siltLayer = roleLayer( TerrainLayerRole::lakeSilt );
	source.bedRules = &m_layerRules;
	source.staticWater = m_staticWater;
	source.hydrology = m_hasHydrology ? &m_hydrology.detailTiles : nullptr;
	source.heights = m_cpuSize == m_masks.size && !m_cpuHeights.empty() ? m_cpuHeights.data() : nullptr;
	source.heightMultiplier = m_heightMultiplier;
	source.heightOffset = m_heightOffset;
	source.waterLevel = &m_waterLevel;
	source.paint = &m_paint;
	return source;
}

bool TerrainPipeline::buildDetailSplat()
{
	// Плитки мазков тонкой сетки: плитка поправок (tileSize её текселей) лежит в одной плитке детального слоя (сторона —
	// detailTileCells · fineScale текселей, строки тонкой сетки — от дальнего края)
	std::set<TerrainDetailSplat::TileKey> extra;
	const uint32_t fine = m_masks.size * TerrainPaint::fineScale;
	const uint32_t tile = TerrainHydrology::detailTileCells * TerrainPaint::fineScale;
	for( const auto& [target, count] : m_paint.targets() )
		if( TerrainPaint::isFine( target ) )
			for( const auto& [key, delta] : m_paint.targetTiles( target ) )
			{
				const uint32_t x = static_cast<uint32_t>( key.first ) * TerrainPaint::tileSize;
				const uint32_t row = static_cast<uint32_t>( key.second ) * TerrainPaint::tileSize;
				if( x < fine && row < fine )
					extra.insert( { x / tile, ( fine - 1 - row ) / tile } );
			}
	m_dirtySplatTiles.clear();
	return m_detailSplat.build( detailSplatSource(), m_detailTilesPerSide,
								std::vector<TerrainDetailSplat::TileKey>( extra.begin(), extra.end() ) );
}

float TerrainPipeline::fineHeight( float x, float z ) const
{
	if( m_detailTileSize > 0.0f )
	{
		const int tx = static_cast<int>( std::floor( x / m_detailTileSize ) );
		const int tz = static_cast<int>( std::floor( z / m_detailTileSize ) );
		if( tx >= 0 && tz >= 0 && tx < static_cast<int>( m_detailTilesPerSide ) && tz < static_cast<int>( m_detailTilesPerSide ) )
		{
			const int slice = m_detailIndex[static_cast<size_t>( tz ) * m_detailTilesPerSide + tx];
			if( slice >= 0 )
			{
				// Как SampleLevel с линейным фильтром и clamp: тексель i — центр в ( i + 0,5 ) текселей от края среза
				const float cell = m_detailTileSize / ( m_detailSide - 2 );
				const float u = ( x - tx * m_detailTileSize ) / cell + 1.0f - 0.5f;
				const float v = ( z - tz * m_detailTileSize ) / cell + 1.0f - 0.5f;
				const int last = static_cast<int>( m_detailSide ) - 1;
				const int i0 = std::clamp( static_cast<int>( std::floor( u ) ), 0, last );
				const int j0 = std::clamp( static_cast<int>( std::floor( v ) ), 0, last );
				const int i1 = std::min( i0 + 1, last );
				const int j1 = std::min( j0 + 1, last );
				const float fu = std::clamp( u - std::floor( u ), 0.0f, 1.0f );
				const float fv = std::clamp( v - std::floor( v ), 0.0f, 1.0f );
				const std::vector<float>& h = m_detailHeights[slice];
				auto at = [&]( int i, int j ) { return h[static_cast<size_t>( j ) * m_detailSide + i]; };
				const float bottom = at( i0, j0 ) + ( at( i1, j0 ) - at( i0, j0 ) ) * fu;
				const float top = at( i0, j1 ) + ( at( i1, j1 ) - at( i0, j1 ) ) * fu;
				return bottom + ( top - bottom ) * fv;
			}
		}
	}
	// Карта высот билинейно по центрам текселей (строки сверху вниз: v = 1 − z / worldSize)
	const int size = static_cast<int>( m_cpuSize );
	const float u = x / m_texelSize - 0.5f;
	const float v = ( m_worldSize - z ) / m_texelSize - 0.5f;
	const int c0 = std::clamp( static_cast<int>( std::floor( u ) ), 0, size - 1 );
	const int r0 = std::clamp( static_cast<int>( std::floor( v ) ), 0, size - 1 );
	const int c1 = std::min( c0 + 1, size - 1 );
	const int r1 = std::min( r0 + 1, size - 1 );
	const float fu = std::clamp( u - std::floor( u ), 0.0f, 1.0f );
	const float fv = std::clamp( v - std::floor( v ), 0.0f, 1.0f );
	auto at = [&]( int c, int r ) { return m_cpuHeights[static_cast<size_t>( r ) * m_cpuSize + c]; };
	const float upper = at( c0, r0 ) + ( at( c1, r0 ) - at( c0, r0 ) ) * fu;
	const float lower = at( c0, r1 ) + ( at( c1, r1 ) - at( c0, r1 ) ) * fu;
	return upper + ( lower - upper ) * fv;
}

bool TerrainPipeline::createFoliageClearMask( const TerrainEditCoverage& coverage )
{
	const size_t size = coverage.clearsFoliage() ? coverage.size : 1;
	ScratchImage mask;
	if( FAILED( mask.Initialize2D( DXGI_FORMAT_R8_UNORM, size, size, 1, 1 ) ) )
		return false;
	const Image& image = *mask.GetImage( 0, 0, 0 );
	for( size_t row = 0; row < size; ++row )
	{
		uint8_t* line = image.pixels + row * image.rowPitch;
		for( size_t col = 0; col < size; ++col )
			line[col] = coverage.clearsFoliage() ?
				static_cast<uint8_t>( std::lround( std::clamp( coverage.foliageClear[row * size + col], 0.0f, 1.0f ) * 255.0f ) ) : 0;
	}
	if( !GpuImages::createTexture( mask, m_foliageClearTexture, m_foliageClearMask ) )
	{
		LOG( "Terrain: can`t create foliage clear mask" );
		return false;
	}
	DMD3D::instance().setName( m_foliageClearTexture, "Terrain foliage clear mask" );
	return true;
}

bool TerrainPipeline::surfaceHeight( float x, float z, float& height ) const
{
	if( m_cpuHeights.empty() || x < 0.0f || z < 0.0f || x > m_worldSize || z > m_worldSize )
		return false;
	// Уровень шага детальной плитки (−2): квад — четверть текселя, вершина — высота детальной земли в своей точке (плитка
	// у русла или карта высот билинейно), диагональ квада — от (0, 0) к (1, 1), как в GridMesh. Мельче (−3, −4) — та же
	// земля билинейно и дисплейсмент у камеры: ходьба его не видит (±несколько сантиметров)
	const float quad = m_texelSize / static_cast<float>( TerrainHydrology::detailSamples );
	const float gx = x / quad;
	const float gz = z / quad;
	const float kx = std::floor( gx );
	const float kz = std::floor( gz );
	const float fx = gx - kx;
	const float fz = gz - kz;
	const float h00 = fineHeight( kx * quad, kz * quad );
	const float h10 = fineHeight( ( kx + 1.0f ) * quad, kz * quad );
	const float h01 = fineHeight( kx * quad, ( kz + 1.0f ) * quad );
	const float h11 = fineHeight( ( kx + 1.0f ) * quad, ( kz + 1.0f ) * quad );
	const float value = fx >= fz ? h00 + fx * ( h10 - h00 ) + fz * ( h11 - h10 ) : h00 + fz * ( h01 - h00 ) + fx * ( h11 - h01 );
	height = value * m_heightMultiplier + m_heightOffset;
	return true;
}

bool TerrainPipeline::erode( HeightField& field, const TerrainErosionSettings& erosion )
{
	const uint32_t size = field.size;
	std::vector<float> source( static_cast<size_t>( size ) * size );
	for( uint32_t row = 0; row < size; ++row )
		for( uint32_t col = 0; col < size; ++col )
			source[static_cast<size_t>( row ) * size + col] = field.heights[row * field.rowPitch + col] * field.heightMultiplier + field.heightOffset;

	// Ключ кэша: исходная карта, масштаб и параметры; версия — при правке алгоритма (terrain_erosion.cs)
	Fingerprint key;
	const char version[] = "erosion-v1";
	key.add( version, sizeof( version ) );
	key.add( source.data(), source.size() * sizeof( float ) );
	key.add( field.texelSize );
	TerrainErosionSettings keyed = erosion;
	keyed.id = 0;
	key.add( &keyed, sizeof( keyed ) );
	char keyText[32];
	std::snprintf( keyText, sizeof( keyText ), "%016llx", static_cast<unsigned long long>( key.value ) );

	const std::string directory = m_erodedDirectory;
	const std::string heightPath = directory + "\\height.dds";
	std::string cachedKey;
	{
		std::ifstream keyFile( directory + "\\key.txt" );
		std::getline( keyFile, cachedKey );
	}
	std::vector<float> eroded;
	if( cachedKey == keyText && loadFloatDDS( heightPath, size, eroded ) )
	{
		LOG( "Terrain erosion: from cache " + directory );
		// Карты эрозии — входы правил слоёв (TerrainMasks); нет файла — правило без карты
		if( !loadFloatDDS( directory + "\\wear.dds", size, m_erosionWear ) || !loadFloatDDS( directory + "\\deposition.dds", size, m_erosionDeposition ) ||
			!loadFloatDDS( directory + "\\talus.dds", size, m_erosionTalus ) )
			LOG( "Terrain erosion: no erosion maps in " + directory + ", layer rules go without them" );
	}
	else
	{
		// Прежний рельеф после эрозии — для пересадки моделей: previous.dds живёт, пока пересадку не сохранят («Save level»,
		// confirmErosionChange); пересчитали ещё раз до сохранения — модели стоят на самом раннем, он и остаётся
		const std::string previousPath = directory + "\\previous.dds";
		std::error_code error;
		if( !std::filesystem::exists( previousPath, error ) && std::filesystem::exists( heightPath, error ) )
			std::filesystem::rename( heightPath, previousPath, error );
		TerrainErosion::Result result;
		if( !TerrainErosion::run( source, size, field.texelSize, erosion, result ) )
		{
			LOG( "Terrain erosion: failed" );
			return false;
		}
		eroded = std::move( result.height );
		m_erosionWear = result.wear;
		m_erosionDeposition = result.deposition;
		m_erosionTalus = result.talus;

		// Кэш и карты эрозии для офлайн-сценариев (Tools/gen_terrain_textures.py): высота, водосбор, размыв, отложения, осыпь
		std::vector<float> normalized( eroded.size() );
		for( size_t i = 0; i < eroded.size(); ++i )
			normalized[i] = ( eroded[i] - field.heightOffset ) / field.heightMultiplier;
		HeightField erodedField = field;
		erodedField.heights = normalized.data();
		erodedField.rowPitch = size;
		const std::vector<float> flow = TerrainHydrology::catchment( erodedField );
		std::filesystem::create_directories( directory, error );
		if( saveFloatDDS( heightPath, size, eroded ) && saveFloatDDS( directory + "\\flow.dds", size, flow ) &&
			saveFloatDDS( directory + "\\wear.dds", size, result.wear ) && saveFloatDDS( directory + "\\deposition.dds", size, result.deposition ) &&
			saveFloatDDS( directory + "\\talus.dds", size, result.talus ) )
		{
			std::ofstream( directory + "\\key.txt" ) << keyText << "\n";
			LOG( "Terrain erosion: written " + directory );
		}
		else
			LOG( "Terrain erosion: can`t write " + directory );
	}
	// Пересадка, не закреплённая сохранением, — на каждой загрузке, пока её не сохранят
	std::vector<float> previous;
	if( loadFloatDDS( directory + "\\previous.dds", size, previous ) )
	{
		if( previous != eroded )
		{
			m_previousEroded = std::move( previous );
			m_eroded = eroded;
		}
		else	// пересчитали в тот же рельеф — модели и так на нём
			confirmErosionChange();
	}
	for( uint32_t row = 0; row < size; ++row )
		for( uint32_t col = 0; col < size; ++col )
			field.heights[row * field.rowPitch + col] = ( eroded[static_cast<size_t>( row ) * size + col] - field.heightOffset ) / field.heightMultiplier;
	return true;
}

float TerrainPipeline::erosionShift( float x, float z, float radius ) const
{
	if( m_previousEroded.empty() )
		return 0.0f;
	return footprintTop( m_eroded, m_cpuSize, m_texelSize, x, z, radius ) - footprintTop( m_previousEroded, m_cpuSize, m_texelSize, x, z, radius );
}

void TerrainPipeline::confirmErosionChange()
{
	std::error_code error;
	std::filesystem::remove( m_erodedDirectory + "\\previous.dds", error );
}

void TerrainPipeline::addErosionProperties( const TerrainErosionSettings& erosion )
{
	m_erosionProperties.setName( "Erosion" );
	addSettingProperties( m_erosionProperties, terrainErosionSchema(), erosion );
}

void TerrainPipeline::addPaintProperties()
{
	// Кисть слоёв и масок (View → «Terrain paint», вьюпорт): с уровнем не сохраняется
	m_paintProperties.setName( "Paint" );
	m_paintProperties.setTransient( true );
	std::vector<std::string> names;
	for( const std::string& target : paintTargets() )
	{
		const int32_t layer = TerrainPaint::layerOf( target );
		names.push_back( layer >= 0 ? target + ": " + m_material.layerName( layer ) : target );
	}
	m_paintProperties.insert( "Target", static_cast<int32_t>( 0 ) )->setItems( names )
		->setTooltip( "What the brush paints: a ground layer or a mask (grass, flowers, forest by kind, shrubs)" );
	m_paintProperties.insert( "Mode", static_cast<int32_t>( 0 ) )->setItems( { "Value", "Step" } )
		->setTooltip( "Value - a stroke takes the result to Value and no further; Step - every stroke adds Value (Shift - subtracts). "
					  "Ctrl - erase the paint back to the rules" );
	Property* prop = m_paintProperties.insert( "Value", 1.0f );
	prop->setLow( 0.0f );
	prop->setHigh( 1.0f );
	prop = m_paintProperties.insert( "Radius", 10.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 200.0f );
	prop->setLogarithmic()->setUnit( "m" );
	prop = m_paintProperties.insert( "Falloff", 0.5f );
	prop->setLow( 0.0f );
	prop->setHigh( 1.0f );
	prop->setTooltip( "Soft edge, share of the radius" );
	m_paintProperties.insert( "Show target", true )->setTooltip( "In the Terrain paint mode - the brush target over the ground: orange by "
																 "its value (layer weight, wetness, mask), dark lines every 0.25" );
}

void TerrainPipeline::addLayerRulesProperties( const TerrainLayerRules& rules )
{
	// Поля правил — одним описанием (terrainLayerRulesSchema): подокно на группу, диапазон — парой полей «от, до»
	m_layerRulesProperties.setName( "Layer rules" );
	for( const SettingField<TerrainLayerRules>& field : terrainLayerRulesSchema() )
	{
		if( std::any_of( m_layerRuleGroups.begin(), m_layerRuleGroups.end(), [&field]( const auto& entry ) { return entry.first == field.group; } ) )
			continue;
		m_layerRuleGroups.emplace_back( field.group, std::make_unique<PropertyContainer>( field.group ) );
		m_layerRulesProperties.addSubContainer( m_layerRuleGroups.back().second.get() );
		addSettingProperties( *m_layerRuleGroups.back().second, terrainLayerRulesSchema(), rules, field.group.c_str() );
	}
}

TerrainLayerRules TerrainPipeline::layerRulesFromProperties() const
{
	TerrainLayerRules rules = m_layerRules;
	for( const auto& [group, properties] : m_layerRuleGroups )
		readSettingProperties( *properties, terrainLayerRulesSchema(), rules, group.c_str() );
	return rules;
}

}
