#include "FarTerrain.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <DirectXTex.h>
#include "CDLODTerrain.h"
#include "TerrainPipeline.h"
#include "D3D\TextureImages.h"
#include "ConstantBuffers.h"
#include "Logger\Logger.h"
#include "Properties\SettingsProperties.h"
#include "Shaders\slots.h"
#include "Shaders\cdlod_constants.h"
#include "Shaders\far_terrain_constants.h"

using namespace DirectX;

namespace
{

constexpr uint32_t farTerrainConstantSlot = 4;	// FarTerrainBuffer — b4 (far_terrain_constants.h)

// Уклон в градусах → косинус угла нормали с вертикалью; «до» — не меньше «от» на сотую градуса
XMFLOAT2 slopeCosines( const SettingRange& degrees )
{
	const float from = std::clamp( degrees.from, 0.0f, 90.0f );
	const float to = std::clamp( std::max( degrees.to, from + 0.01f ), 0.0f, 90.0f );
	return XMFLOAT2( std::cos( XMConvertToRadians( from ) ), std::cos( XMConvertToRadians( to ) ) );
}

XMFLOAT2 heightRange( const SettingRange& range )
{
	return XMFLOAT2( range.from, std::max( range.to, range.from + 0.01f ) );
}

}

namespace GS
{

FarTerrain::FarTerrain() :
	SceneObject( "Far terrain" )
{
}

bool FarTerrain::initialize( const FarTerrainSettings& settings, CDLODTerrain& terrain, TerrainPipeline& pipeline )
{
	m_terrain = &terrain;
	m_pipeline = &pipeline;
	m_loaded = settings;
	if( !loadHeights( settings ) )
		return false;

	// Одна сетка узлов с картой: тексель гор — лист карты, карта — целый узел своего корня, начало гор кратно ему
	const float leaf = terrain.leafSize();
	const float cell = settings.extent / static_cast<float>( m_size );
	const float mapRoot = leaf * static_cast<float>( 1u << ( terrain.levelCount() - 1 ) );
	const auto multipleOf = []( float value, float step ) { return std::abs( std::remainder( value, step ) ) < 1e-3f * step; };
	if( std::abs( cell - leaf ) > 1e-3f * leaf || std::abs( pipeline.worldSize() - mapRoot ) > 1e-3f * mapRoot ||
		!multipleOf( settings.origin.x, mapRoot ) || !multipleOf( settings.origin.y, mapRoot ) || settings.origin.x > 0.0f ||
		settings.origin.y > 0.0f || settings.origin.x + settings.extent < pipeline.worldSize() ||
		settings.origin.y + settings.extent < pipeline.worldSize() )
	{
		LOG( "Far terrain: its texel (" + std::to_string( cell ) + " m) must be the terrain leaf (" + std::to_string( leaf ) +
			 " m), the level map - a whole node (" + std::to_string( mapRoot ) + " m), the origin - a multiple of it, around the map" );
		return false;
	}
	m_mipBias = static_cast<uint32_t>( std::lround( std::log2( leaf / pipeline.texelSize() ) ) );

	m_quadtree.initialize( settings.origin, leaf, settings.extent );
	m_quadtree.setHole( XMFLOAT2( 0.0f, 0.0f ), XMFLOAT2( pipeline.worldSize(), pipeline.worldSize() ) );

	// Окно — до запекания: границы узлов у края карты берут полосу перехода из него
	m_properties.setName( "Far terrain" );
	addSettingProperties( m_properties, farTerrainSchema(), settings );
	m_properties.insert( "Wireframe", false );
	m_properties.insert( "Show LOD", false );

	if( !createShader() || !bake() )
		return false;

	m_patch.initialize( CDLODTerrain::patchDim + 1, CDLODTerrain::patchDim + 1 );
	m_patchBuffer.createBuffer( sizeof( Patch ), CDLODQuadtree::maxPatches, "Far terrain patches" );
	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( CDLODBuffer ), m_terrainConstants ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( FarTerrainBuffer ), m_constants ) )
		return false;

	// Дальность гор — в таблицу карты до первого кадра
	m_terrain->setFarLevels( m_mipBias, settings.lodDistance, m_heightExtent );

	LOG( "Far terrain: " + std::to_string( m_size ) + " texels of " + std::to_string( cell ) + " m from (" +
		 std::to_string( settings.origin.x ) + ", " + std::to_string( settings.origin.y ) + "), LOD levels " +
		 std::to_string( m_quadtree.levelCount() ) );
	m_initialized = true;
	return true;
}

bool FarTerrain::loadHeights( const FarTerrainSettings& settings )
{
	if( settings.heightMapFile.empty() )
	{
		LOG( "Far terrain: heightmap " + settings.heightMap + " is not found in table Textures" );
		return false;
	}
	const std::filesystem::path path = std::filesystem::path( "Textures" ) / settings.heightMapFile;
	ScratchImage image;
	if( FAILED( LoadFromDDSFile( path.wstring().c_str(), DDS_FLAGS_NONE, nullptr, image ) ) || image.GetImageCount() == 0 )
	{
		LOG( "Far terrain: can`t load " + path.string() );
		return false;
	}
	const Image& source = *image.GetImage( 0, 0, 0 );
	if( source.width != source.height || ( source.width & ( source.width - 1 ) ) != 0 ||
		( source.format != DXGI_FORMAT_R16_UNORM && source.format != DXGI_FORMAT_R32_FLOAT ) )
	{
		LOG( "Far terrain: " + path.string() + " must be a square power of two, R16_UNORM or R32_FLOAT" );
		return false;
	}
	m_size = static_cast<uint32_t>( source.width );
	m_source.resize( static_cast<size_t>( m_size ) * m_size );
	for( uint32_t row = 0; row < m_size; ++row )
	{
		const uint8_t* line = source.pixels + row * source.rowPitch;
		for( uint32_t col = 0; col < m_size; ++col )
		{
			float value;
			if( source.format == DXGI_FORMAT_R16_UNORM )
				value = static_cast<float>( reinterpret_cast<const uint16_t*>( line )[col] ) / 65535.0f;
			else
				value = reinterpret_cast<const float*>( line )[col];
			m_source[static_cast<size_t>( row ) * m_size + col] = value * settings.heightMultiplier + settings.heightOffset;
		}
	}
	return true;
}

bool FarTerrain::createShader()
{
	std::vector<VertexElement> layout = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } };
	m_shader.setLayoutDesc( std::move( layout ) );

	if( !m_shader.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\far_terrain.vs" ) ||
		!m_shader.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\far_terrain.ps" ) ||
		!m_shader.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\cdlod_lod.ps" ) )
	{
		LOG( "Far terrain: shader compilation failed" );
		return false;
	}
	m_materialPhase = m_shader.createPhase( 0, 0 );
	m_lodPhase = m_shader.createPhase( 0, 1 );
	m_depthPhase = m_shader.createPhase( 0, -1 );
	return m_materialPhase >= 0 && m_lodPhase >= 0 && m_depthPhase >= 0;
}

bool FarTerrain::bake()
{
	m_bakedRevision = m_pipeline->heightRevision();
	m_bakedMultiplier = m_pipeline->heightMultiplier();
	m_bakedOffset = m_pipeline->heightOffset();
	++m_bakes;
	m_heights = m_source;

	// Внутри карты — её мип с текселем гор (те же значения, что читают её вершины этого уровня): мипы гор у края — по нынешней
	// карте, а не по той, с которой их считал генератор
	const std::vector<TerrainPipeline::HeightMip>& mips = m_pipeline->heightMips();
	const float cell = m_loaded.extent / static_cast<float>( m_size );
	const uint32_t mapTexels = static_cast<uint32_t>( std::lround( m_pipeline->worldSize() / cell ) );
	const int col0 = static_cast<int>( std::lround( -m_loaded.origin.x / cell ) );
	const int row0 = static_cast<int>( std::lround( ( m_loaded.origin.y + m_loaded.extent - m_pipeline->worldSize() ) / cell ) );
	if( m_mipBias < mips.size() && mips[m_mipBias].size == mapTexels )
	{
		const TerrainPipeline::HeightMip& mip = mips[m_mipBias];
		for( uint32_t row = 0; row < mapTexels; ++row )
			for( uint32_t col = 0; col < mapTexels; ++col )
				m_heights[static_cast<size_t>( row0 + row ) * m_size + col0 + col] =
					mip.values[static_cast<size_t>( row ) * mip.size + col] * m_bakedMultiplier + m_bakedOffset;
	}
	else
		LOG( "Far terrain: the level map has no mip of the far texel, its old heights stay inside" );
	m_mapHeights = XMFLOAT2( FLT_MAX, -FLT_MAX );
	if( !mips.empty() )
		for( const float value : mips[0].values )
		{
			m_mapHeights.x = std::min( m_mapHeights.x, value * m_bakedMultiplier + m_bakedOffset );
			m_mapHeights.y = std::max( m_mapHeights.y, value * m_bakedMultiplier + m_bakedOffset );
		}

	// Пирамида min / max — границы узлов
	m_minPyramid.assign( 1, {} );
	m_maxPyramid.assign( 1, {} );
	for( uint32_t size = m_size / 2, k = 1; size >= 1; size /= 2, ++k )
	{
		const std::vector<float>& lowerMin = k == 1 ? m_heights : m_minPyramid[k - 1];
		const std::vector<float>& lowerMax = k == 1 ? m_heights : m_maxPyramid[k - 1];
		std::vector<float> levelMin( static_cast<size_t>( size ) * size );
		std::vector<float> levelMax( levelMin.size() );
		for( uint32_t row = 0; row < size; ++row )
		{
			for( uint32_t col = 0; col < size; ++col )
			{
				const size_t a = static_cast<size_t>( row * 2 ) * ( size * 2 ) + col * 2;
				const size_t b = a + size * 2;
				levelMin[static_cast<size_t>( row ) * size + col] = std::min( { lowerMin[a], lowerMin[a + 1], lowerMin[b], lowerMin[b + 1] } );
				levelMax[static_cast<size_t>( row ) * size + col] = std::max( { lowerMax[a], lowerMax[a + 1], lowerMax[b], lowerMax[b + 1] } );
			}
		}
		m_minPyramid.push_back( std::move( levelMin ) );
		m_maxPyramid.push_back( std::move( levelMax ) );
	}

	// Перепады узлов уровней гор (с m_mipBias) — нижняя граница их дальностей; ближние уровни — таблица карты
	m_heightExtent.assign( m_quadtree.levelCount(), 0.0f );
	for( uint32_t level = m_mipBias; level < m_quadtree.levelCount(); ++level )
	{
		const uint32_t count = m_quadtree.nodesPerSide( level );
		for( uint32_t z = 0; z < count; ++z )
			for( uint32_t x = 0; x < count; ++x )
			{
				const Box box = node( level, x, z );
				m_heightExtent[level] = std::max( m_heightExtent[level], box.max.y - box.min.y );
			}
	}

	// Текстура — R32_FLOAT в метрах с полной цепочкой мипов (средние): вершины уровня L читают мип L − m_mipBias
	ScratchImage image;
	if( FAILED( image.Initialize2D( DXGI_FORMAT_R32_FLOAT, m_size, m_size, 1, 1 ) ) )
		return false;
	const Image& top = *image.GetImage( 0, 0, 0 );
	for( uint32_t row = 0; row < m_size; ++row )
		std::memcpy( top.pixels + row * top.rowPitch, &m_heights[static_cast<size_t>( row ) * m_size], m_size * sizeof( float ) );
	ScratchImage mipChain;
	if( FAILED( GenerateMipMaps( top, TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC, 0, mipChain ) ) ||
		!GpuImages::createTexture( mipChain, m_heightTexture, m_heightView ) )
	{
		LOG( "Far terrain: can`t create the height texture" );
		return false;
	}
	DMD3D::instance().setName( m_heightTexture, "Far terrain heights" );
	m_maxHeight = *std::max_element( m_heights.begin(), m_heights.end() );
	return true;
}

XMFLOAT2 FarTerrain::pyramidBounds( uint32_t k, int x0, int x1, int z0, int z1 ) const
{
	const int size = static_cast<int>( m_size >> k );
	const std::vector<float>& minValues = k == 0 ? m_heights : m_minPyramid[k];
	const std::vector<float>& maxValues = k == 0 ? m_heights : m_maxPyramid[k];
	XMFLOAT2 bounds( FLT_MAX, -FLT_MAX );
	for( int z = std::max( z0, 0 ); z <= std::min( z1, size - 1 ); ++z )
	{
		const size_t row = static_cast<size_t>( size - 1 - z ) * size;	// ряды пирамиды — с севера
		for( int x = std::max( x0, 0 ); x <= std::min( x1, size - 1 ); ++x )
		{
			bounds.x = std::min( bounds.x, minValues[row + x] );
			bounds.y = std::max( bounds.y, maxValues[row + x] );
		}
	}
	return bounds;
}

CDLODQuadtree::Box FarTerrain::node( uint32_t level, uint32_t x, uint32_t z ) const
{
	// Тексели гор узла с запасом в тексель уровня пирамиды вдвое мельче узла: вершины уровня читают мип уровня и следующего
	// билинейно — не дальше этого запаса
	const uint32_t k = level > 0 ? level - 1 : 0;
	const int span = static_cast<int>( 1u << ( level - k ) );
	XMFLOAT2 bounds = pyramidBounds( k, static_cast<int>( x ) * span - 1, static_cast<int>( x + 1 ) * span,
									 static_cast<int>( z ) * span - 1, static_cast<int>( z + 1 ) * span );

	const float size = m_quadtree.nodeSize( level );
	const XMFLOAT2& origin = m_quadtree.origin();
	Box box;
	box.min = XMFLOAT3( origin.x + x * size, bounds.x, origin.y + z * size );
	box.max = XMFLOAT3( origin.x + ( x + 1 ) * size, bounds.y, origin.y + ( z + 1 ) * size );
	// У края карты высота переходит в её высоту: и её пределы
	const float world = m_pipeline->worldSize();
	const float dx = std::max( { 0.0f - box.max.x, box.min.x - world, 0.0f } );
	const float dz = std::max( { 0.0f - box.max.z, box.min.z - world, 0.0f } );
	if( dx * dx + dz * dz < m_properties["Blend distance"].data<float>() * m_properties["Blend distance"].data<float>() )
	{
		box.min.y = std::min( box.min.y, m_mapHeights.x );
		box.max.y = std::max( box.max.y, m_mapHeights.y );
	}
	return box;
}

CDLODQuadtree::Box FarTerrain::detailNode( uint32_t depth, uint32_t nx, uint32_t nz ) const
{
	// Высоты — его листа
	Box box = node( 0, nx >> depth, nz >> depth );
	const float size = m_quadtree.nodeSize( 0 ) / static_cast<float>( 1u << depth );
	const XMFLOAT2& origin = m_quadtree.origin();
	box.min.x = origin.x + nx * size;
	box.min.z = origin.y + nz * size;
	box.max.x = box.min.x + size;
	box.max.z = box.min.z + size;
	return box;
}

void FarTerrain::update( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	// Карту построили заново (перестройка русел) или сменили её множитель — её мип в горах заново
	if( m_bakedRevision != m_pipeline->heightRevision() || m_bakedMultiplier != m_pipeline->heightMultiplier() ||
		m_bakedOffset != m_pipeline->heightOffset() )
		bake();
	// Дальность гор — в таблицу карты (её посчитает карта в следующем кадре); таблица — общая
	m_terrain->setFarLevels( m_mipBias, m_properties["LOD distance"].data<float>(), m_heightExtent );
	m_quadtree.setLod( m_terrain->lod() );
}

void FarTerrain::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	// Виды теней собираются после главного: его узлы не трогают
	if( !m_initialized || view.index != 0 )
		return;
	m_patches.clear();
	m_quadtree.select( view, *this, m_patches );
	if( !m_patches.empty() )
		collector.addCustom( passBit( MeshPass::depthPrepass ) | passBit( MeshPass::opaque ) );
}

void FarTerrain::renderCustom( const RenderContext& context )
{
	const bool depthOnly = isDepthOnlyPass( context.pass );

	ScopedRenderState state;
	if( m_properties["Wireframe"].data<bool>() )
		DMD3D::instance().setState( RasterState::wireframe );

	// Параметры карты и таблица дальностей — как у её рельефа: высота на стыке — тем же кодом с теми же данными
	Device::updateResource<CDLODBuffer>( m_terrainConstants, [this, &context]( CDLODBuffer& params )
	{
		m_terrain->fillConstants( params, context.view, -1 );
	} );
	DMD3D::instance().setConstantBuffer( SLOT_CB_MATERIAL, m_terrainConstants );

	Device::updateResource<FarTerrainBuffer>( m_constants, [this]( FarTerrainBuffer& params )
	{
		const FarTerrainSettings rules = settings();
		params.farOrigin = rules.origin;
		params.farExtent = rules.extent;
		params.farMipBias = static_cast<float>( m_mipBias );
		params.farBlendDistance = std::max( rules.blendDistance, 1.0f );
		params.farMaterialBlend = std::max( rules.materialBlend, 0.0f );
		params.rockSlope = slopeCosines( rules.rockSlope );
		params.screeSlope = slopeCosines( rules.screeSlope );
		params.screeHeight = heightRange( rules.screeHeight );
		params.meadowHeight = heightRange( rules.meadowHeight );
		params.snowHeight = heightRange( rules.snowHeight );
		params.snowSlope = slopeCosines( rules.snowSlope );
		params.snowNorth = rules.snowNorth;
		params.snowNoise = rules.snowNoise;
		params.forestLine = heightRange( rules.forestLine );
		params.forestSlope = slopeCosines( rules.forestSlope );
		params.forestColor = rules.forestColor;
		params.grassWetLayer = m_pipeline->roleLayer( TerrainLayerRole::grassWet );
		params.grassDryLayer = m_pipeline->roleLayer( TerrainLayerRole::grassDry );
		params.talusLayer = m_pipeline->roleLayer( TerrainLayerRole::talus );
		params.rockLayer = m_pipeline->roleLayer( TerrainLayerRole::rock );
		params.snowLayer = m_pipeline->roleLayer( TerrainLayerRole::snow );
		params.macroDistance = heightRange( rules.macroDistance );
		params.macroTextureScale = std::max( rules.macroTextureScale, 1.0f );
	} );
	DMD3D::instance().setConstantBuffer( farTerrainConstantSlot, m_constants );

	m_patchBuffer.updateData( m_patches.data(), sizeof( Patch ) * m_patches.size() );
	m_patchBuffer.setToSlot( SLOT_INSTANCE_DATA );

	// Карта высот уровня — у края, карта гор — дальше; слои — пиксельному шейдеру
	DMD3D::instance().setSRV( 0, m_pipeline->heightMapView() );
	DMD3D::instance().setSRV( 1, m_heightView );
	if( !depthOnly )
		m_pipeline->material().bind();

	context.constants.setPerObjectBuffer( XMMatrixIdentity() );
	DMD3D::instance().setVertexBuffer( m_patch.vertexBuffer(), sizeof( XMFLOAT3 ) );
	DMD3D::instance().setIndexBuffer( m_patch.indexBuffer(), DXGI_FORMAT_R32_UINT );

	m_shader.setPass( depthOnly ? m_depthPhase : m_properties["Show LOD"].data<bool>() ? m_lodPhase : m_materialPhase );
	DMD3D::instance().drawIndexedInstanced( m_patch.indexCount(), static_cast<uint32_t>( m_patches.size() ), 0, 0 );
}

void FarTerrain::warmPipelines( const PassStates& states )
{
	if( !m_initialized )
		return;
	// Как у рельефа карты, без теней: цвет — с записью глубины или на равенство после prepass, prepass — только глубина
	m_shader.warmPipelines( { { RasterState::solid, DepthState::enabled, BlendState::opaque },
							  { RasterState::solid, DepthState::readOnlyEqual, BlendState::opaque },
							  { RasterState::wireframe, DepthState::enabled, BlendState::opaque },
							  { RasterState::wireframe, DepthState::readOnlyEqual, BlendState::opaque } }, states.scene,
							{ m_materialPhase, m_lodPhase } );
	m_shader.warmPipelines( { { RasterState::solid, DepthState::enabled, BlendState::opaque },
							  { RasterState::wireframe, DepthState::enabled, BlendState::opaque } }, states.depthOnly, { m_depthPhase } );
}

FarTerrainHeight FarTerrain::farHeight() const
{
	FarTerrainHeight height;
	if( !m_initialized )
		return height;
	height.heightMap = &m_heightView;
	height.origin = m_loaded.origin;
	height.extent = m_loaded.extent;
	height.size = m_size;
	height.maxHeight = m_maxHeight;
	height.revision = m_bakes;
	height.shadowSize = static_cast<uint32_t>( std::max( settings().shadowResolution, 0 ) );	// из окна — номер значения списка
	return height;
}

FarTerrainSettings FarTerrain::settings() const
{
	FarTerrainSettings settings = m_loaded;
	readSettingProperties( m_properties, farTerrainSchema(), settings );
	return settings;
}

}
