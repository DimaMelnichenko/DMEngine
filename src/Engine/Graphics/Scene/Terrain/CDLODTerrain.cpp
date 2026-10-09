#include "CDLODTerrain.h"
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
#include "System.h"
#include "ConstantBuffers.h"
#include "DBConnector.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace
{

bool sphereIntersectsBox( const XMFLOAT3& center, float radius, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax )
{
	float dx = std::max( { boxMin.x - center.x, 0.0f, center.x - boxMax.x } );
	float dy = std::max( { boxMin.y - center.y, 0.0f, center.y - boxMax.y } );
	float dz = std::max( { boxMin.z - center.z, 0.0f, center.z - boxMax.z } );
	return dx * dx + dy * dy + dz * dz <= radius * radius;
}

}

namespace GS
{

CDLODTerrain::CDLODTerrain() :
	SceneObject( "CDLOD terrain" )
{
}

bool CDLODTerrain::initialize( uint32_t terrainId, const std::vector<TerrainEdit>& edits, const WaterSimulationSettings* water,
							   const TerrainErosionSettings* erosion )
{
	if( erosion )
		m_erosion = *erosion;
	TerrainSettings settings;
	std::string splatMap;
	if( !loadSettings( terrainId, settings, splatMap ) )
		return false;
	m_terrainId = terrainId;
	m_heightMultiplier = settings.heightMultiplier;

	const uint32_t mapSize = System::textures().get( m_heightMapName )->width();
	m_worldSize = mapSize * m_texelSize;

	// Уровней столько, чтобы корень покрыл всю карту высот
	m_levelCount = 1;
	while( m_levelCount < maxLevels && ( gridDim << ( m_levelCount - 1 ) ) < mapSize )
		++m_levelCount;

	m_nodesPerSide.resize( m_levelCount );
	for( uint32_t level = 0; level < m_levelCount; ++level )
		m_nodesPerSide[level] = static_cast<uint32_t>( std::ceil( m_worldSize / nodeSize( level ) ) );

	TerrainEditCoverage coverage;
	if( !buildHeightBounds( edits, water, coverage ) || !createFoliageClearMask( coverage ) )
		return false;

	if( !createShader() )
		return false;

	if( !m_material.initialize( terrainId, splatMap, coverage ) )
		return false;

	m_patch.initialize( patchDim + 1, patchDim + 1 );
	m_patchBuffer.createBuffer( sizeof( PatchInstance ), maxPatches, "Terrain patches" );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) )
		return false;

	m_properties.setName( "CDLOD terrain" );

	auto prop = m_properties.insert( "LOD distance", nodeSize( 0 ) * 4.0f );
	prop->setLow( nodeSize( 0 ) );
	prop->setHigh( nodeSize( 0 ) * 64.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Height multiplier", settings.heightMultiplier );
	prop->setLow( 1.0f );
	prop->setHigh( 5000.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setUnit( "m" )->setTooltip( "Height of the heightmap value 1; the scatter and particles follow, the terrain edits do not" );

	prop = m_properties.insert( "Triplanar sharpness", settings.triplanarSharpness );
	prop->setLow( 1.0f );
	prop->setHigh( 32.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setTooltip( "How sharply steep slopes switch from the top projection to the side ones" );

	prop = m_properties.insert( "Height blend", settings.heightBlend );
	prop->setLow( 0.01f );
	prop->setHigh( 1.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setTooltip( "Depth of the height-based blend between layers" );

	// Второй масштаб текстур вдали (distance resampling, как в материалах UE Landscape): мелкий повтор слоёв
	// издалека складывается в сетку
	prop = m_properties.insert( "Far texture scale", settings.farTextureScale );
	prop->setLow( 1.0f );
	prop->setHigh( 32.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setTooltip( "Texture repeat multiplier far away: the small repeat does not fold into a grid" );

	prop = m_properties.insert( "Far blend start", settings.farBlendStart );
	prop->setLow( 0.0f );
	prop->setHigh( 500.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setUnit( "m" );

	prop = m_properties.insert( "Far blend end", settings.farBlendEnd );
	prop->setLow( 1.0f );
	prop->setHigh( 1000.0f );
	prop->setControlType( GUIControlType::SLIDER );
	prop->setUnit( "m" );

	// Метров на повтор текстуры слоя (TerrainLayers.tiling)
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
	m_properties.addSubContainer( &m_layerProperties );
	if( m_erosion )
		addErosionProperties( *m_erosion );

	m_properties.insert( "Wireframe", false );
	m_properties.insert( "Show LOD", false );
	// Вода симуляции (WaterSimulation, SLOT_WATER) поверх земли: синее — глубже, светлее — быстрее
	m_properties.insert( "Show water", false );

	LOG( "CDLOD terrain: heightmap " + std::to_string( mapSize ) + ", LOD levels " + std::to_string( m_levelCount ) );

	m_initialized = true;

	return true;
}

bool CDLODTerrain::loadEditRaster( TerrainEdit& edit )
{
	// Карта сдвига высоты, м: R32_FLOAT квадратом, путь — от Textures\ (как у текстур хранилища)
	const std::wstring path = L"Textures\\" + std::wstring( edit.raster.begin(), edit.raster.end() );
	ScratchImage image;
	if( FAILED( LoadFromDDSFile( path.c_str(), DDS_FLAGS_NONE, nullptr, image ) ) || image.GetImageCount() == 0 ||
		image.GetMetadata().format != DXGI_FORMAT_R32_FLOAT || image.GetMetadata().width != image.GetMetadata().height )
	{
		LOG( "CDLOD terrain: raster of terrain edit " + edit.name + " (" + edit.raster + ") is not loaded: square R32_FLOAT DDS expected" );
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

TerrainHeight CDLODTerrain::terrainHeight() const
{
	TerrainHeight height;
	height.heightMap = &m_heightMap;
	height.foliageClearMask = &m_foliageClearMask;
	height.worldSize = m_worldSize;
	height.mapSize = static_cast<uint32_t>( std::lround( m_worldSize / m_texelSize ) );
	// Ползунок, а не m_heightMultiplier: тот обновляется в update() только у видимого террейна
	height.heightMultiplier = m_initialized ? m_properties["Height multiplier"].data<float>() : m_heightMultiplier;
	height.heightOffset = m_heightOffset;
	height.detailTileSize = m_detailTileSize;
	return height;
}

bool CDLODTerrain::loadSettings( uint32_t terrainId, TerrainSettings& settings, std::string& splatMap )
{
	SQLite::Statement query( DBConnector::instance().db(), "select heightmap, splatmap, height_multiplier, height_offset, width_multiplier, "
														   "triplanar_sharpness, height_blend, far_texture_scale, far_blend_start, "
														   "far_blend_end from Terrain where id = :id" );
	query.bind( ":id", terrainId );

	if( !query.executeStep() )
	{
		LOG( "CDLOD terrain: no row " + std::to_string( terrainId ) + " in table Terrain" );
		return false;
	}

	m_heightMapName = query.getColumn( "heightmap" ).getString();
	splatMap = query.getColumn( "splatmap" ).getString();
	settings.id = terrainId;
	settings.heightMultiplier = static_cast<float>( query.getColumn( "height_multiplier" ).getDouble() );
	// Материал: NULL — значения по умолчанию TerrainSettings
	auto material = [&query]( const char* column, float& value )
	{
		if( !query.getColumn( column ).isNull() )
			value = static_cast<float>( query.getColumn( column ).getDouble() );
	};
	material( "triplanar_sharpness", settings.triplanarSharpness );
	material( "height_blend", settings.heightBlend );
	material( "far_texture_scale", settings.farTextureScale );
	material( "far_blend_start", settings.farBlendStart );
	material( "far_blend_end", settings.farBlendEnd );
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
	return true;
}

bool CDLODTerrain::createShader()
{
	std::vector<VertexElement> layout = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } };
	m_shader.setLayoutDesc( std::move( layout ) );

	if( !m_shader.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\cdlod.vs" ) ||
		!m_shader.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\terrain.ps" ) ||
		!m_shader.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\cdlod_lod.ps" ) )
	{
		LOG( "CDLOD terrain: shader compilation failed" );
		return false;
	}

	// Фазы: материал террейна, раскраска по уровням LOD, только глубина (тени)
	m_materialPhase = m_shader.createPhase( 0, 0 );
	m_lodPhase = m_shader.createPhase( 0, 1 );
	m_depthPhase = m_shader.createPhase( 0, -1 );
	if( m_materialPhase < 0 || m_lodPhase < 0 || m_depthPhase < 0 )
		return false;
	return true;
}

void CDLODTerrain::warmPipelines( const PassStates& states )
{
	if( !m_initialized )
		return;
	// Цвет: глубина с записью или проверка на равенство после depth prepass; prepass — только глубина; тени — состояние
	// прохода теней рендерера (растеризатор со смещением глубины солнца)
	m_shader.warmPipelines( { { RasterState::solid, DepthState::enabled, BlendState::opaque },
							  { RasterState::solid, DepthState::readOnlyEqual, BlendState::opaque },
							  { RasterState::wireframe, DepthState::enabled, BlendState::opaque },
							  { RasterState::wireframe, DepthState::readOnlyEqual, BlendState::opaque } }, states.scene,
							{ m_materialPhase, m_lodPhase } );
	m_shader.warmPipelines( { { RasterState::solid, DepthState::enabled, BlendState::opaque },
							  { RasterState::wireframe, DepthState::enabled, BlendState::opaque },
							  states.shadowDepth }, states.depthOnly, { m_depthPhase } );
}

bool CDLODTerrain::buildHeightBounds( const std::vector<TerrainEdit>& edits, const WaterSimulationSettings* water, TerrainEditCoverage& coverage )
{
	ScratchImage captured;
	if( !GpuImages::captureTexture( System::textures().get( m_heightMapName )->texture(), captured ) )
	{
		LOG( "CDLOD terrain: can`t copy heightmap to CPU" );
		return false;
	}

	const Image* image = captured.GetImage( 0, 0, 0 );

	ScratchImage decompressed;
	if( IsCompressed( image->format ) )
	{
		if( FAILED( Decompress( *image, DXGI_FORMAT_UNKNOWN, decompressed ) ) )
		{
			LOG( "CDLOD terrain: can`t decompress heightmap" );
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
			LOG( "CDLOD terrain: can`t convert heightmap to R32_FLOAT" );
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
			LOG( "CDLOD terrain: can`t convert heightmap to R32_FLOAT" );
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
	if( m_erosion && !erode( field, *m_erosion ) )
		return false;
	if( !edits.empty() )
	{
		std::vector<TerrainEdit> loaded = edits;
		for( TerrainEdit& edit : loaded )
			if( !edit.raster.empty() )
				loadEditRaster( edit );
		const size_t changed = applyTerrainEdits( field, loaded, &coverage );
		LOG( "CDLOD terrain: " + std::to_string( edits.size() ) + " terrain edits, texels changed: " + std::to_string( changed ) );
	}
	// Основа для перестройки русел без перезагрузки (rebuildChannels): рельеф после эрозии и ручных правок и их покрытие
	m_baseSize = field.size;
	m_baseHeights.resize( static_cast<size_t>( field.size ) * field.size );
	for( uint32_t row = 0; row < field.size; ++row )
		std::memcpy( &m_baseHeights[static_cast<size_t>( row ) * field.size], field.heights + row * field.rowPitch, field.size * sizeof( float ) );
	m_baseCoverage = coverage;

	m_hasHydrology = false;
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
				LOG( "CDLOD terrain: stream curves are not generated" );
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
	return finishHeights( *image, water, coverage );
}

bool CDLODTerrain::finishHeights( const Image& image, const WaterSimulationSettings* water, TerrainEditCoverage& coverage )
{
	HeightField field;
	field.heights = reinterpret_cast<float*>( image.pixels );
	field.size = static_cast<uint32_t>( image.width );
	field.rowPitch = image.rowPitch / sizeof( float );
	field.texelSize = m_texelSize;
	field.heightMultiplier = m_heightMultiplier;
	field.heightOffset = m_heightOffset;
	m_hasHydrology = false;
	if( water )
	{
		const auto start = std::chrono::steady_clock::now();
		if( !TerrainHydrology::build( field, *water, m_streamCurves, m_hydrology ) )
		{
			LOG( "CDLOD terrain: water channels are not built" );
			return false;
		}
		applyTerrainEdits( field, { TerrainHydrology::loweringEdit( m_hydrology, water->channels ) }, &coverage );
		m_hasHydrology = true;
		for( const std::string& warning : m_hydrology.warnings )
			LOG( "Terrain hydrology: " + warning );
		char text[256];
		std::snprintf( text, sizeof( text ), "Terrain hydrology: largest discharge %.3f m3/s, channel nodes %zu, carved cells %zu, "
					   "deepest %.2f m, streams %zu, points %zu, ms: %.1f", m_hydrology.largestDischarge, m_hydrology.nodes,
					   m_hydrology.carvedCells, -m_hydrology.deepestLowering, m_hydrology.streams.size(), m_hydrology.points,
					   std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
		LOG( text );
	}
	if( !createDetailTiles() )
		return false;

	// Копия итоговой карты на CPU — высота поверхности для ходьбы (surfaceHeight); 1024² — 4 МБ
	m_cpuSize = static_cast<uint32_t>( image.width );
	m_cpuHeights.resize( static_cast<size_t>( m_cpuSize ) * image.height );
	for( size_t row = 0; row < image.height; ++row )
		std::memcpy( &m_cpuHeights[row * m_cpuSize], image.pixels + row * image.rowPitch, m_cpuSize * sizeof( float ) );

	// Вершины уровня L читают мипы L и L + 1, поэтому вершинному шейдеру нужна карта высот с полной цепочкой мипов
	ScratchImage mipChain;
	if( FAILED( GenerateMipMaps( image, TEX_FILTER_DEFAULT, 0, mipChain ) ) )
	{
		LOG( "CDLOD terrain: can`t generate heightmap mips" );
		return false;
	}

	if( !GpuImages::createTexture( mipChain, m_heightMapTexture, m_heightMap ) )
	{
		LOG( "CDLOD terrain: can`t create heightmap texture" );
		return false;
	}
	DMD3D::instance().setName( m_heightMapTexture, "Terrain height map" );

	const uint32_t lastMip = static_cast<uint32_t>( mipChain.GetMetadata().mipLevels ) - 1;

	// Нормированные min / max высоты по всем текселям мипа, которые билинейно читают вершины прямоугольника
	// [x0, x1] × [z0, z1] (в долях стороны террейна), с запасом в тексель по краям.
	// Ось z мира идёт по текстуре снизу вверх: в шейдере v = 1 - z / worldSize
	auto mipBounds = [&mipChain]( uint32_t mip, float x0, float x1, float z0, float z1, XMFLOAT2& bounds )
	{
		const Image* mipImage = mipChain.GetImage( mip, 0, 0 );
		const int width = static_cast<int>( mipImage->width );
		const int height = static_cast<int>( mipImage->height );

		const int colMin = std::max( static_cast<int>( std::floor( x0 * width ) ) - 1, 0 );
		const int colMax = std::min( static_cast<int>( std::ceil( x1 * width ) ) + 1, width - 1 );
		const int rowMin = std::max( static_cast<int>( std::floor( ( 1.0f - z1 ) * height ) ) - 1, 0 );
		const int rowMax = std::min( static_cast<int>( std::ceil( ( 1.0f - z0 ) * height ) ) + 1, height - 1 );

		for( int row = rowMin; row <= rowMax; ++row )
		{
			const float* line = reinterpret_cast<const float*>( mipImage->pixels + row * mipImage->rowPitch );
			for( int col = colMin; col <= colMax; ++col )
			{
				bounds.x = std::min( bounds.x, line[col] );
				bounds.y = std::max( bounds.y, line[col] );
			}
		}
	};

	m_heightBounds.assign( m_levelCount, {} );

	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		const uint32_t count = m_nodesPerSide[level];
		const float size = nodeSize( level ) / m_worldSize;
		m_heightBounds[level].resize( count * count );

		for( uint32_t z = 0; z < count; ++z )
		{
			for( uint32_t x = 0; x < count; ++x )
			{
				XMFLOAT2 bounds( FLT_MAX, -FLT_MAX );
				mipBounds( std::min( level, lastMip ), x * size, ( x + 1 ) * size, z * size, ( z + 1 ) * size, bounds );
				mipBounds( std::min( level + 1, lastMip ), x * size, ( x + 1 ) * size, z * size, ( z + 1 ) * size, bounds );
				m_heightBounds[level][z * count + x] = bounds;
			}
		}
	}

	return true;
}

bool CDLODTerrain::rebuildChannels( const WaterSimulationSettings& water, const std::vector<StreamCurve>& curves )
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
	return finishHeights( target, &water, coverage ) && createFoliageClearMask( coverage ) && m_material.repaint( coverage );
}

bool CDLODTerrain::createDetailTiles()
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
	if( FAILED( array.Initialize2D( DXGI_FORMAT_R32_FLOAT, tiles.empty() ? 1 : side, tiles.empty() ? 1 : side, slices, 1 ) ) )
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
			std::memcpy( target.pixels + row * target.rowPitch, &heights[static_cast<size_t>( row ) * side], side * sizeof( float ) );
		if( tile.x < m_detailTilesPerSide && tile.z < m_detailTilesPerSide )
			m_detailIndex[static_cast<size_t>( tile.z ) * m_detailTilesPerSide + tile.x] = static_cast<int>( t );
	}
	if( !GpuImages::createTexture( array, m_detailTexture, m_detailView, TextureViewDesc::Kind::texture2DArray ) )
	{
		LOG( "CDLOD terrain: can`t create detail tiles" );
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
		LOG( "CDLOD terrain: can`t create detail tile index" );
		return false;
	}
	DMD3D::instance().setName( m_detailIndexTexture, "Terrain detail tile index" );
	if( !tiles.empty() )
		LOG( "CDLOD terrain: detail tiles " + std::to_string( tiles.size() ) + " (" + std::to_string( side ) + "x" + std::to_string( side ) +
			 ", " + std::to_string( tiles.size() * side * side * sizeof( float ) / 1024 / 1024 ) + " MB)" );
	return true;
}

float CDLODTerrain::fineHeight( float x, float z ) const
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

void CDLODTerrain::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	DMD3D::instance().setSRV( SLOT_TERRAIN_DETAIL, m_detailView );
	DMD3D::instance().setSRV( SLOT_TERRAIN_DETAIL_INDEX, m_detailIndexView );
}

bool CDLODTerrain::createFoliageClearMask( const TerrainEditCoverage& coverage )
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
		LOG( "CDLOD terrain: can`t create foliage clear mask" );
		return false;
	}
	DMD3D::instance().setName( m_foliageClearTexture, "Terrain foliage clear mask" );
	return true;
}

void CDLODTerrain::calcRanges()
{
	// Трещин нет, если на границе уровней L и L + 1 вершины уровня L уже полностью морфированы, а уровня L + 1 ещё
	// не начали. Граница удалена от камеры не больше чем на диапазон L плюс диагональ узла L (с перепадом высот),
	// а морфинг уровня L + 1 начинается через morphStartRatio диапазона L после конца уровня L. Отсюда нижняя граница
	// диапазона: диагональ / morphStartRatio. Меньшее значение "LOD distance" не применяется
	float minRange = 0.0f;
	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		float heightExtent = 0.0f;
		for( const XMFLOAT2& bounds : m_heightBounds[level] )
			heightExtent = std::max( heightExtent, ( bounds.y - bounds.x ) * m_heightMultiplier );

		const float size = nodeSize( level );
		const float diagonal = std::sqrt( 2.0f * size * size + heightExtent * heightExtent );
		minRange = std::max( minRange, diagonal / morphStartRatio / static_cast<float>( 1u << level ) );
	}

	float range = std::max( m_properties["LOD distance"].data<float>(), minRange );
	float previousRange = 0.0f;
	for( uint32_t level = 0; level < m_levelCount; ++level )
	{
		const float morphStart = previousRange + ( range - previousRange ) * morphStartRatio;
		m_ranges[level] = range;
		m_morphConsts[level] = XMFLOAT4( morphStart, 1.0f / ( range - morphStart ), 0.0f, 0.0f );

		previousRange = range;
		range *= 2.0f;
	}

	// Уровни мельче листа: диапазон −1 — половина диапазона листа, −2 — четверть, не меньше диагонали узла / morphStartRatio
	// (условие без трещин, как выше). Морфинг листа начинается дальше 0,66 его диапазона — за диапазоном −1
	float detailPrevious = 0.0f;
	float detailRange[detailLevels];
	for( uint32_t depth = 1; depth <= detailLevels; ++depth )
	{
		const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
		detailRange[depth - 1] = std::max( m_ranges[0] / static_cast<float>( 1u << depth ), std::sqrt( 2.0f ) * size / morphStartRatio );
	}
	for( uint32_t depth = detailLevels; depth >= 1; --depth )
	{
		const float rangeAt = std::min( detailRange[depth - 1], m_ranges[0] * 0.5f );
		const float morphStart = detailPrevious + ( rangeAt - detailPrevious ) * morphStartRatio;
		m_detailRanges[depth - 1] = rangeAt;
		m_detailMorph[depth - 1] = XMFLOAT4( morphStart, 1.0f / std::max( rangeAt - morphStart, 1e-3f ), 0.0f, 0.0f );
		detailPrevious = rangeAt;
	}
}

float CDLODTerrain::nodeSize( uint32_t level ) const
{
	return gridDim * m_texelSize * static_cast<float>( 1u << level );
}

CDLODTerrain::NodeBox CDLODTerrain::nodeBox( uint32_t level, uint32_t x, uint32_t z ) const
{
	const float size = nodeSize( level );
	const XMFLOAT2& bounds = m_heightBounds[level][z * m_nodesPerSide[level] + x];

	NodeBox box;
	box.min = XMFLOAT3( x * size, bounds.x * m_heightMultiplier + m_heightOffset, z * size );
	box.max = XMFLOAT3( std::min( ( x + 1 ) * size, m_worldSize ),
						bounds.y * m_heightMultiplier + m_heightOffset,
						std::min( ( z + 1 ) * size, m_worldSize ) );
	return box;
}

void CDLODTerrain::update( const FrameContext& frame )
{
	if( !m_initialized )
		return;

	m_heightMultiplier = m_properties["Height multiplier"].data<float>();
	calcRanges();
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
	{
		const std::string name = std::to_string( layer ) + ": " + m_material.layerName( layer );
		if( m_layerProperties.exists( name ) )
			m_material.setTiling( layer, std::max( m_layerProperties[name].data<float>(), 0.01f ) );
	}
}

bool CDLODTerrain::surfaceHeight( float x, float z, float& height ) const
{
	if( m_cpuHeights.empty() || x < 0.0f || z < 0.0f || x > m_worldSize || z > m_worldSize )
		return false;
	// Самый детальный уровень у камеры (−2): квад — четверть текселя, вершина — высота детальной земли в своей точке
	// (плитка у русла или карта высот билинейно), диагональ квада — от (0, 0) к (1, 1), как в GridMesh
	const float quad = m_texelSize / static_cast<float>( 1u << detailLevels );
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

namespace
{

// Отпечаток FNV-1a 64 — ключ кэша эрозии
struct Fingerprint
{
	uint64_t value = 1469598103934665603ull;
	void add( const void* data, size_t size )
	{
		const uint8_t* bytes = static_cast<const uint8_t*>( data );
		for( size_t i = 0; i < size; ++i )
			value = ( value ^ bytes[i] ) * 1099511628211ull;
	}
	template<typename T> void add( const T& v ) { add( &v, sizeof( v ) ); }
};

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

bool CDLODTerrain::erode( HeightField& field, const TerrainErosionSettings& erosion )
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

float CDLODTerrain::erosionShift( float x, float z, float radius ) const
{
	if( m_previousEroded.empty() )
		return 0.0f;
	return footprintTop( m_eroded, m_cpuSize, m_texelSize, x, z, radius ) - footprintTop( m_previousEroded, m_cpuSize, m_texelSize, x, z, radius );
}

void CDLODTerrain::confirmErosionChange()
{
	std::error_code error;
	std::filesystem::remove( m_erodedDirectory + "\\previous.dds", error );
}

void CDLODTerrain::addErosionProperties( const TerrainErosionSettings& erosion )
{
	m_erosionProperties.setName( "Erosion" );
	auto number = [this]( const char* name, float value, float low, float high, const char* tooltip )
	{
		Property* property = m_erosionProperties.insert( name, value );
		property->setLow( low );
		property->setHigh( std::max( high, value ) );
		property->setTooltip( std::string( tooltip ) + "; applied at the next level load" );
		return property;
	};
	auto integer = [this]( const char* name, int32_t value, float low, float high, const char* tooltip )
	{
		Property* property = m_erosionProperties.insert( name, value );
		property->setLow( low );
		property->setHigh( std::max( high, static_cast<float>( value ) ) );
		property->setTooltip( std::string( tooltip ) + "; applied at the next level load" );
		return property;
	};
	integer( "Droplets", erosion.droplets, 0.0f, 4000000.0f, "Water droplets of the hydraulic erosion" );
	integer( "Seed", static_cast<int32_t>( erosion.seed ), 0.0f, 1000.0f, "Random numbers of the droplets" );
	integer( "Lifetime", erosion.lifetime, 1.0f, 300.0f, "Steps of a droplet, a texel each" );
	number( "Inertia", erosion.inertia, 0.0f, 1.0f, "Share of the previous direction" );
	number( "Capacity", erosion.capacity, 0.0f, 8.0f, "Sediment capacity: slope * speed * water * capacity" );
	number( "Min slope", erosion.minSlope, 0.0f, 0.1f, "Slope in the capacity at least" );
	number( "Erode speed", erosion.erodeSpeed, 0.0f, 1.0f, "Share of the missing capacity eroded per step" );
	number( "Deposit speed", erosion.depositSpeed, 0.0f, 1.0f, "Share of the excess sediment deposited per step" );
	number( "Evaporation", erosion.evaporation, 0.0f, 0.1f, "Water evaporated per step" );
	number( "Gravity", erosion.gravity, 0.0f, 20.0f, "Speed-up downhill" );
	integer( "Radius", erosion.radius, 1.0f, 12.0f, "Erosion brush, texels" );
	number( "Rain scale", erosion.rainScale, 10.0f, 2000.0f, "Size of the rain patches, m" )->setUnit( "m" );
	number( "Rain min", erosion.rainMin, 0.0f, 1.0f, "Rain share in dry places (1 - even rain)" );
	number( "Talus angle", erosion.talusAngle, 10.0f, 80.0f, "Angle of repose of the scree" )->setUnit( "deg" );
	integer( "Thermal iterations", erosion.thermalIterations, 0.0f, 200.0f, "Steps of the scree sliding" );
	number( "Thermal rate", erosion.thermalRate, 0.0f, 1.0f, "Share of the excess sliding per step" );
	m_properties.addSubContainer( &m_erosionProperties );
}

TerrainSettings CDLODTerrain::settings() const
{
	TerrainSettings settings;
	settings.id = m_terrainId;
	settings.heightMultiplier = m_properties["Height multiplier"].data<float>();
	settings.triplanarSharpness = m_properties["Triplanar sharpness"].data<float>();
	settings.heightBlend = m_properties["Height blend"].data<float>();
	settings.farTextureScale = m_properties["Far texture scale"].data<float>();
	settings.farBlendStart = m_properties["Far blend start"].data<float>();
	settings.farBlendEnd = m_properties["Far blend end"].data<float>();
	settings.layerTiling.assign( m_material.layerCount(), 0.0f );
	for( uint32_t layer = 0; layer < m_material.layerCount(); ++layer )
		if( !m_material.layerName( layer ).empty() )
			settings.layerTiling[layer] = m_material.tiling( layer );
	if( m_erosion )
	{
		TerrainErosionSettings& erosion = settings.erosion.emplace( *m_erosion );
		const PropertyContainer& p = m_erosionProperties;
		erosion.droplets = std::max( p["Droplets"].data<int32_t>(), 0 );
		erosion.seed = static_cast<uint32_t>( std::max( p["Seed"].data<int32_t>(), 0 ) );
		erosion.lifetime = std::max( p["Lifetime"].data<int32_t>(), 1 );
		erosion.inertia = p["Inertia"].data<float>();
		erosion.capacity = p["Capacity"].data<float>();
		erosion.minSlope = p["Min slope"].data<float>();
		erosion.erodeSpeed = p["Erode speed"].data<float>();
		erosion.depositSpeed = p["Deposit speed"].data<float>();
		erosion.evaporation = p["Evaporation"].data<float>();
		erosion.gravity = p["Gravity"].data<float>();
		erosion.radius = std::max( p["Radius"].data<int32_t>(), 1 );
		erosion.rainScale = p["Rain scale"].data<float>();
		erosion.rainMin = p["Rain min"].data<float>();
		erosion.talusAngle = p["Talus angle"].data<float>();
		erosion.thermalIterations = std::max( p["Thermal iterations"].data<int32_t>(), 0 );
		erosion.thermalRate = p["Thermal rate"].data<float>();
	}
	return settings;
}

void CDLODTerrain::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	std::vector<PatchInstance>& patches = m_patches[view.index];
	patches.clear();
	if( !m_initialized )
		return;

	const uint32_t top = m_levelCount - 1;
	for( uint32_t z = 0; z < m_nodesPerSide[top]; ++z )
	{
		for( uint32_t x = 0; x < m_nodesPerSide[top]; ++x )
		{
			selectNode( view, top, x, z, patches );
		}
	}

	if( !patches.empty() )
		collector.addCustom( passBit( MeshPass::depthPrepass ) | passBit( MeshPass::opaque ) | passBit( MeshPass::csmShadowDepth ) );
}

bool CDLODTerrain::selectNode( const RenderView& view, uint32_t level, uint32_t x, uint32_t z,
							   std::vector<PatchInstance>& patches )
{
	const NodeBox box = nodeBox( level, x, z );
	const XMFLOAT3& cameraPosition = view.lodOrigin;

	if( !sphereIntersectsBox( cameraPosition, m_ranges[level], box.min, box.max ) )
		return false;

	// Невидимый узел считается обработанным: рисовать его не нужно ни ему, ни родителю
	if( !view.frustum.checkBox( box.min, box.max ) )
		return true;

	// Лист у камеры — уровни мельче листа (−1, −2) везде: квадродерево однородно, переходы без трещин
	if( level == 0 && sphereIntersectsBox( cameraPosition, m_detailRanges[0], box.min, box.max ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			if( !selectDetailNode( view, 1, x * 2 + ( quarter & 1 ), z * 2 + ( quarter >> 1 ), patches ) )
				addPatch( 0, x, z, quarter, patches );
		return true;
	}
	if( level == 0 || !sphereIntersectsBox( cameraPosition, m_ranges[level - 1], box.min, box.max ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			addPatch( level, x, z, quarter, patches );
		return true;
	}

	// Четверти, которые дочерний узел не взял (он дальше своего диапазона), рисуются с детализацией этого уровня
	const uint32_t childCount = m_nodesPerSide[level - 1];
	for( uint32_t quarter = 0; quarter < 4; ++quarter )
	{
		const uint32_t cx = x * 2 + ( quarter & 1 );
		const uint32_t cz = z * 2 + ( quarter >> 1 );
		if( cx >= childCount || cz >= childCount )
			continue;

		if( !selectNode( view, level - 1, cx, cz, patches ) )
			addPatch( level, x, z, quarter, patches );
	}

	return true;
}

void CDLODTerrain::addPatch( uint32_t level, uint32_t x, uint32_t z, uint32_t quarter, std::vector<PatchInstance>& patches )
{
	const float size = nodeSize( level );
	const float halfSize = size * 0.5f;
	const XMFLOAT2 origin( x * size + ( quarter & 1 ) * halfSize, z * size + ( quarter >> 1 ) * halfSize );

	if( origin.x >= m_worldSize || origin.y >= m_worldSize || patches.size() >= maxPatches )
		return;

	patches.push_back( { origin, halfSize, static_cast<float>( level ) } );
}

bool CDLODTerrain::selectDetailNode( const RenderView& view, uint32_t depth, uint32_t nx, uint32_t nz, std::vector<PatchInstance>& patches )
{
	// Высоты узла — его листа и детальной плитки листа, если она есть
	const uint32_t leafX = nx >> depth;
	const uint32_t leafZ = nz >> depth;
	if( leafX >= m_nodesPerSide[0] || leafZ >= m_nodesPerSide[0] )
		return true;
	XMFLOAT2 bounds = m_heightBounds[0][leafZ * m_nodesPerSide[0] + leafX];
	if( m_detailTileSize > 0.0f )
	{
		const int slice = m_detailIndex[static_cast<size_t>( leafZ ) * m_detailTilesPerSide + leafX];
		if( slice >= 0 )
			bounds = XMFLOAT2( std::min( bounds.x, m_detailBounds[slice].x ), std::max( bounds.y, m_detailBounds[slice].y ) );
	}
	const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
	const XMFLOAT3 boxMin( nx * size, bounds.x * m_heightMultiplier + m_heightOffset, nz * size );
	const XMFLOAT3 boxMax( std::min( ( nx + 1 ) * size, m_worldSize ), bounds.y * m_heightMultiplier + m_heightOffset,
						   std::min( ( nz + 1 ) * size, m_worldSize ) );
	const XMFLOAT3& origin = view.lodOrigin;
	if( !sphereIntersectsBox( origin, m_detailRanges[depth - 1], boxMin, boxMax ) )
		return false;
	if( !view.frustum.checkBox( boxMin, boxMax ) )
		return true;
	if( depth == detailLevels || !sphereIntersectsBox( origin, m_detailRanges[depth], boxMin, boxMax ) )
	{
		for( uint32_t quarter = 0; quarter < 4; ++quarter )
			addDetailPatch( depth, nx, nz, quarter, patches );
		return true;
	}
	for( uint32_t quarter = 0; quarter < 4; ++quarter )
		if( !selectDetailNode( view, depth + 1, nx * 2 + ( quarter & 1 ), nz * 2 + ( quarter >> 1 ), patches ) )
			addDetailPatch( depth, nx, nz, quarter, patches );
	return true;
}

void CDLODTerrain::addDetailPatch( uint32_t depth, uint32_t nx, uint32_t nz, uint32_t quarter, std::vector<PatchInstance>& patches )
{
	const float size = nodeSize( 0 ) / static_cast<float>( 1u << depth );
	const float halfSize = size * 0.5f;
	const XMFLOAT2 origin( nx * size + ( quarter & 1 ) * halfSize, nz * size + ( quarter >> 1 ) * halfSize );
	if( origin.x >= m_worldSize || origin.y >= m_worldSize || patches.size() >= maxPatches )
		return;
	patches.push_back( { origin, halfSize, -static_cast<float>( depth ) } );
}

void CDLODTerrain::renderCustom( const RenderContext& context )
{
	// В проходах только глубины (тени, depth prepass) — без раскраски LOD, материала и ресурсов пиксельного шейдера.
	// Каркас террейна — и в prepass: иначе сплошная глубина закрыла бы то, что видно сквозь каркас
	const bool depthOnly = isDepthOnlyPass( context.pass );
	std::vector<PatchInstance>& patches = m_patches[context.view.index];

	ScopedRenderState terrainState;
	if( context.pass != MeshPass::csmShadowDepth && m_properties["Wireframe"].data<bool>() )
	{
		DMD3D::instance().setState( RasterState::wireframe );
	}

	Device::updateResource<Parameters>( m_constantBuffer, [this]( Parameters& params )
	{
		params.worldSize = m_worldSize;
		params.heightMultiplier = m_heightMultiplier;
		params.heightOffset = m_heightOffset;
		params.gridDim = static_cast<float>( patchDim );
		std::copy( std::begin( m_morphConsts ), std::end( m_morphConsts ), params.morphConsts );
		std::copy( m_material.layerScale().begin(), m_material.layerScale().end(), params.layerScale );
		params.layerCount = m_material.layerCount();
		params.texelSize = m_texelSize;
		params.triplanarSharpness = m_properties["Triplanar sharpness"].data<float>();
		params.heightBlendDepth = m_properties["Height blend"].data<float>();
		params.farTextureScale = m_properties["Far texture scale"].data<float>();
		params.farBlendStart = m_properties["Far blend start"].data<float>();
		params.farBlendEnd = std::max( m_properties["Far blend end"].data<float>(), params.farBlendStart + 1.0f );
		params.showWater = m_properties["Show water"].data<bool>() ? 1 : 0;
		params.detailTile = m_detailTileSize;
		std::copy( std::begin( m_detailMorph ), std::end( m_detailMorph ), params.detailMorph );
	} );
	DMD3D::instance().setConstantBuffer( SLOT_CB_MATERIAL, m_constantBuffer );

	m_patchBuffer.updateData( patches.data(), sizeof( PatchInstance ) * patches.size() );
	m_patchBuffer.setToSlot( SLOT_INSTANCE_DATA );

	// Карта высот — вершинному шейдеру и пиксельному: по ней считается нормаль рельефа
	DMD3D::instance().setSRV( 0, m_heightMap );
	if( !depthOnly )
		m_material.bind();

	context.constants.setPerObjectBuffer( XMMatrixIdentity() );

	DMD3D::instance().setVertexBuffer( m_patch.vertexBuffer(), sizeof( XMFLOAT3 ) );
	DMD3D::instance().setIndexBuffer( m_patch.indexBuffer(), DXGI_FORMAT_R32_UINT );

	m_shader.setPass( depthOnly ? m_depthPhase : m_properties["Show LOD"].data<bool>() ? m_lodPhase : m_materialPhase );
	DMD3D::instance().drawIndexedInstanced( m_patch.indexCount(), static_cast<uint32_t>( patches.size() ), 0, 0 );
}

PropertyContainer* CDLODTerrain::properties()
{
	return &m_properties;
}

}
