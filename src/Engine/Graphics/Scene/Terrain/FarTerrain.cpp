#include "FarTerrain.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <DirectXTex.h>
#include <fstream>
#include "CDLODTerrain.h"
#include "FluvialErosion.h"
#include "GlacialErosion.h"
#include "TerrainPipeline.h"
#include "Utils\Fingerprint.h"
#include "Utils\ValueNoise.h"
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

// Кубический B-сплайн: веса четырёх текселей при доле t между вторым и третьим
void splineWeights( float t, float weights[4] )
{
	const float u = 1.0f - t;
	weights[0] = u * u * u / 6.0f;
	weights[1] = ( 3.0f * t * t * t - 6.0f * t * t + 4.0f ) / 6.0f;
	weights[2] = ( -3.0f * t * t * t + 3.0f * t * t + 3.0f * t + 1.0f ) / 6.0f;
	weights[3] = t * t * t / 6.0f;
}

// Сетка n² → (n · factor)² кубическим B-сплайном (гладко, без изломов на границах грубых клеток: они были бы видны сеткой
// в свете), за краем — край; строки с севера, центры клеток совпадают
std::vector<float> upsample( const std::vector<float>& coarse, uint32_t n, uint32_t factor )
{
	const uint32_t m = n * factor;
	std::vector<float> rows( static_cast<size_t>( n ) * m );	// по строкам грубой сетки, столбцы — тонкие
	auto at = [n]( int64_t i ) { return static_cast<uint32_t>( std::clamp<int64_t>( i, 0, n - 1 ) ); };
	for( uint32_t col = 0; col < m; ++col )
	{
		const float u = ( col + 0.5f ) / factor - 0.5f;
		const float base = std::floor( u );
		float w[4];
		splineWeights( u - base, w );
		const int64_t i = static_cast<int64_t>( base );
		for( uint32_t row = 0; row < n; ++row )
		{
			const float* line = &coarse[static_cast<size_t>( row ) * n];
			rows[static_cast<size_t>( row ) * m + col] = w[0] * line[at( i - 1 )] + w[1] * line[at( i )] + w[2] * line[at( i + 1 )] +
														 w[3] * line[at( i + 2 )];
		}
	}
	std::vector<float> fine( static_cast<size_t>( m ) * m );
	for( uint32_t row = 0; row < m; ++row )
	{
		const float v = ( row + 0.5f ) / factor - 0.5f;
		const float base = std::floor( v );
		float w[4];
		splineWeights( v - base, w );
		const int64_t j = static_cast<int64_t>( base );
		const float* r0 = &rows[static_cast<size_t>( at( j - 1 ) ) * m];
		const float* r1 = &rows[static_cast<size_t>( at( j ) ) * m];
		const float* r2 = &rows[static_cast<size_t>( at( j + 1 ) ) * m];
		const float* r3 = &rows[static_cast<size_t>( at( j + 2 ) ) * m];
		float* out = &fine[static_cast<size_t>( row ) * m];
		for( uint32_t col = 0; col < m; ++col )
			out[col] = w[0] * r0[col] + w[1] * r1[col] + w[2] * r2[col] + w[3] * r3[col];
	}
	return fine;
}

// Три прохода прямоугольного фильтра радиуса radius по обеим осям — гаусс с сигмой ≈ radius + 0,5; за краем — край
std::vector<float> blur( const std::vector<float>& values, uint32_t size, int32_t radius )
{
	std::vector<float> current = values;
	std::vector<float> next( values.size() );
	const float scale = 1.0f / static_cast<float>( 2 * radius + 1 );
	for( int pass = 0; pass < 3; ++pass )
	{
		for( int axis = 0; axis < 2; ++axis )
		{
			for( uint32_t line = 0; line < size; ++line )
			{
				auto index = [&]( int64_t i )
				{
					const size_t k = static_cast<size_t>( std::clamp<int64_t>( i, 0, size - 1 ) );
					return axis == 0 ? static_cast<size_t>( line ) * size + k : k * size + line;
				};
				float sum = 0.0f;
				for( int32_t k = -radius; k <= radius; ++k )
					sum += current[index( k )];
				for( uint32_t i = 0; i < size; ++i )
				{
					next[index( i )] = sum * scale;
					sum += current[index( static_cast<int64_t>( i ) + radius + 1 )] - current[index( static_cast<int64_t>( i ) - radius )];
				}
			}
			std::swap( current, next );
		}
	}
	return current;
}

bool loadFloats( const std::filesystem::path& path, uint32_t size, std::vector<float>& values )
{
	ScratchImage image;
	if( FAILED( LoadFromDDSFile( path.wstring().c_str(), DDS_FLAGS_NONE, nullptr, image ) ) || image.GetImageCount() == 0 )
		return false;
	const Image& source = *image.GetImage( 0, 0, 0 );
	if( source.width != size || source.height != size || source.format != DXGI_FORMAT_R32_FLOAT )
		return false;
	values.resize( static_cast<size_t>( size ) * size );
	for( uint32_t row = 0; row < size; ++row )
		std::memcpy( &values[static_cast<size_t>( row ) * size], source.pixels + row * source.rowPitch, size * sizeof( float ) );
	return true;
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
	if( settings.fluvial )
	{
		m_fluvialProperties.setName( "Fluvial erosion" );
		addSettingProperties( m_fluvialProperties, fluvialErosionSchema(), *settings.fluvial );
		m_properties.addSubContainer( &m_fluvialProperties );
	}
	if( settings.glacier )
	{
		m_glacierProperties.setName( "Glacial erosion" );
		addSettingProperties( m_glacierProperties, glacialErosionSchema(), *settings.glacier );
		m_properties.addSubContainer( &m_glacierProperties );
	}
	m_erosionRevision = m_fluvialProperties.revision() + m_glacierProperties.revision();

	applyErosion();
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
	m_original.resize( static_cast<size_t>( m_size ) * m_size );
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
			m_original[static_cast<size_t>( row ) * m_size + col] = value * settings.heightMultiplier + settings.heightOffset;
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
	// Правка подокон эрозии закончилась — горы размываются заново
	const uint32_t erosionRevision = m_fluvialProperties.revision() + m_glacierProperties.revision();
	const bool glacierEdited = !m_fluvialProperties.editing() && !m_glacierProperties.editing() && erosionRevision != m_erosionRevision;
	if( glacierEdited )
	{
		m_erosionRevision = erosionRevision;
		applyErosion();
	}
	// Карту построили заново (перестройка русел) или сменили её множитель — её мип в горах заново
	if( glacierEdited || m_bakedRevision != m_pipeline->heightRevision() || m_bakedMultiplier != m_pipeline->heightMultiplier() ||
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
	if( settings.fluvial )
		settings.fluvial = fluvialSettings();
	if( settings.glacier )
		settings.glacier = glacierSettings();
	return settings;
}

FluvialErosionSettings FarTerrain::fluvialSettings() const
{
	FluvialErosionSettings settings = m_loaded.fluvial ? *m_loaded.fluvial : FluvialErosionSettings();
	readSettingProperties( m_fluvialProperties, fluvialErosionSchema(), settings );
	return settings;
}

GlacialErosionSettings FarTerrain::glacierSettings() const
{
	GlacialErosionSettings settings = m_loaded.glacier ? *m_loaded.glacier : GlacialErosionSettings();
	readSettingProperties( m_glacierProperties, glacialErosionSchema(), settings );
	return settings;
}

void FarTerrain::applyErosion()
{
	m_source = m_original;
	if( !m_loaded.fluvial && !m_loaded.glacier )
		return;
	const FluvialErosionSettings fluvial = fluvialSettings();
	const GlacialErosionSettings glacier = glacierSettings();
	// Сетка — степень двойки, не мельче карты гор
	auto gridSize = [this]( int32_t resolution )
	{
		uint32_t size = 1;
		while( size * 2 <= static_cast<uint32_t>( std::max( resolution, 1 ) ) && size * 2 <= m_size )
			size *= 2;
		return size;
	};

	// Ключ кэша: исходная карта, её место, карта уровня и настройки обеих эрозий; версия — при правке алгоритмов
	// (FluvialErosion, glacial_erosion.cs)
	Fingerprint key;
	const char version[] = "mountain-erosion-v4";
	key.add( version, sizeof( version ) );
	key.addWords( m_original.data(), m_original.size() * sizeof( float ) );
	key.add( m_size );
	key.add( m_loaded.origin );
	key.add( m_loaded.extent );
	key.add( m_pipeline->worldSize() );
	key.add( static_cast<uint8_t>( m_loaded.fluvial ? 1 : 0 ) );
	if( m_loaded.fluvial )
	{
		FluvialErosionSettings keyed = fluvial;
		keyed.id = 0;
		key.add( &keyed, sizeof( keyed ) );
	}
	key.add( static_cast<uint8_t>( m_loaded.glacier ? 1 : 0 ) );
	if( m_loaded.glacier )
	{
		GlacialErosionSettings keyed = glacier;
		keyed.id = 0;
		key.add( &keyed, sizeof( keyed ) );
	}
	char keyText[32];
	std::snprintf( keyText, sizeof( keyText ), "%016llx", static_cast<unsigned long long>( key.value ) );

	// Кэш — рядом с картой гор: Textures\<её каталог>\far_eroded
	const std::filesystem::path directory = ( std::filesystem::path( "Textures" ) / m_loaded.heightMapFile ).parent_path() / "far_eroded";
	std::string cachedKey;
	{
		std::ifstream keyFile( directory / "key.txt" );
		std::getline( keyFile, cachedKey );
	}
	if( cachedKey == keyText && loadFloats( directory / "height.dds", m_size, m_source ) )
	{
		LOG( "Mountain erosion: from cache " + directory.string() );
		return;
	}

	// Сетка средних по клеткам и изменение на ней — к карте гор кубическим B-сплайном
	auto downsample = [this]( const std::vector<float>& heights, uint32_t size )
	{
		const uint32_t factor = m_size / size;
		std::vector<float> coarse( static_cast<size_t>( size ) * size, 0.0f );
		const float share = 1.0f / static_cast<float>( factor * factor );
		for( uint32_t row = 0; row < m_size; ++row )
			for( uint32_t col = 0; col < m_size; ++col )
				coarse[static_cast<size_t>( row / factor ) * size + col / factor] += heights[static_cast<size_t>( row ) * m_size + col] * share;
		return coarse;
	};

	// 1. Реки — сеть долин
	if( m_loaded.fluvial )
	{
		const uint32_t size = gridSize( fluvial.resolution );
		std::vector<float> coarse = downsample( m_source, size );
		std::vector<float> eroded = coarse;
		FluvialErosion::run( eroded, size, m_loaded.extent / static_cast<float>( size ), m_loaded.origin, m_pipeline->worldSize(), fluvial );
		for( size_t i = 0; i < coarse.size(); ++i )
			eroded[i] -= coarse[i];
		const std::vector<float> change = size == m_size ? eroded : upsample( eroded, size, m_size / size );
		for( size_t i = 0; i < m_source.size(); ++i )
			m_source[i] += change[i];
	}

	// 2. Ледники — троги по долинам; под льдом мелкий рельеф сглажен (ледник шлифует ложе)
	std::vector<float> iceMax;
	uint32_t iceSize = 0;
	if( m_loaded.glacier )
	{
		iceSize = gridSize( glacier.resolution );
		const uint32_t factor = m_size / iceSize;
		const std::vector<float> coarse = downsample( m_source, iceSize );
		GlacialErosion::Result result;
		if( GlacialErosion::run( coarse, iceSize, m_loaded.extent / static_cast<float>( iceSize ), m_loaded.origin, m_pipeline->worldSize(),
								 glacier, result ) )
		{
			std::vector<float> change( coarse.size() );
			for( size_t i = 0; i < coarse.size(); ++i )
				change[i] = result.bed[i] - coarse[i];
			const std::vector<float> fineChange = upsample( change, iceSize, factor );
			const std::vector<float> fineIce = upsample( result.iceMax, iceSize, factor );
			const std::vector<float> smooth = blur( m_source, m_size, 2 );
			const float polish = std::clamp( glacier.polish, 0.0f, 1.0f );
			const float cell = m_loaded.extent / static_cast<float>( m_size );
			const float fadeFrom = glacier.mapFade.from;
			const float fadeTo = std::max( glacier.mapFade.to, fadeFrom + 1.0f );
			const float world = m_pipeline->worldSize();
			for( uint32_t row = 0; row < m_size; ++row )
			{
				for( uint32_t col = 0; col < m_size; ++col )
				{
					const size_t i = static_cast<size_t>( row ) * m_size + col;
					const float t = std::clamp( ( fineIce[i] - 10.0f ) / 90.0f, 0.0f, 1.0f );
					const float iceCover = t * t * ( 3.0f - 2.0f * t );
					// Шлифовка у карты гаснет, как и размыв (glacial_erosion.cs)
					const float x = m_loaded.origin.x + ( col + 0.5f ) * cell;
					const float z = m_loaded.origin.y + ( m_size - row - 0.5f ) * cell;
					const float dx = std::max( { -x, x - world, 0.0f } );
					const float dz = std::max( { -z, z - world, 0.0f } );
					const float f = std::clamp( ( std::sqrt( dx * dx + dz * dz ) - fadeFrom ) / ( fadeTo - fadeFrom ), 0.0f, 1.0f );
					const float fade = f * f * ( 3.0f - 2.0f * f );
					m_source[i] += fineChange[i] - polish * iceCover * fade * ( m_source[i] - smooth[i] );
				}
			}
			iceMax = std::move( result.iceMax );
		}
		else
			LOG( "Glacial erosion: failed, the mountains stay without it" );

		// 3. Морозное выветривание: зубцы по выпуклым гребням (выше сглаженной на ~200 м земли) и крутым склонам высоко над
		// морем — острый «гребенчатый» шум (1 − |2n − 1|)³ двух октав; у карты гаснет только у стыка (frostPeakFade)
		const float amplitude = std::max( glacier.frostPeaks, 0.0f );
		if( amplitude > 0.0f )
		{
			const std::vector<float> around = blur( m_source, m_size, 6 );
			const float spacing = std::max( glacier.frostPeakSpacing, 1.0f );
			const float heightFrom = glacier.frostPeakHeight.from;
			const float heightTo = std::max( glacier.frostPeakHeight.to, heightFrom + 1.0f );
			const float cell = m_loaded.extent / static_cast<float>( m_size );
			const float fadeFrom = glacier.frostPeakFade.from;
			const float fadeTo = std::max( glacier.frostPeakFade.to, fadeFrom + 1.0f );
			const float world = m_pipeline->worldSize();
			auto smooth01 = []( float t )
			{
				t = std::clamp( t, 0.0f, 1.0f );
				return t * t * ( 3.0f - 2.0f * t );
			};
			for( uint32_t row = 0; row < m_size; ++row )
			{
				for( uint32_t col = 0; col < m_size; ++col )
				{
					const size_t i = static_cast<size_t>( row ) * m_size + col;
					const float x = m_loaded.origin.x + ( col + 0.5f ) * cell;
					const float z = m_loaded.origin.y + ( m_size - row - 0.5f ) * cell;
					// Гребни — выше сглаженной земли; склоны — круче 30…45° по ней же (на ориентире скальные выступы торчат и из
					// снежных склонов, не только по кромке)
					const uint32_t left = col > 0 ? col - 1 : col, right = col + 1 < m_size ? col + 1 : col;
					const uint32_t up = row > 0 ? row - 1 : row, down = row + 1 < m_size ? row + 1 : row;
					const float gx = ( around[static_cast<size_t>( row ) * m_size + right] - around[static_cast<size_t>( row ) * m_size + left] ) /
									 ( ( right - left ) * cell );
					const float gz = ( around[static_cast<size_t>( up ) * m_size + col] - around[static_cast<size_t>( down ) * m_size + col] ) /
									 ( ( down - up ) * cell );
					const float steep = smooth01( ( std::sqrt( gx * gx + gz * gz ) - 0.58f ) / 0.42f );
					const float ridge = std::max( smooth01( ( m_source[i] - around[i] - 5.0f ) / 40.0f ), steep );
					const float high = smooth01( ( m_source[i] - heightFrom ) / ( heightTo - heightFrom ) );
					if( ridge <= 0.0f || high <= 0.0f )
						continue;
					const float dx = std::max( { -x, x - world, 0.0f } );
					const float dz = std::max( { -z, z - world, 0.0f } );
					const float fade = smooth01( ( std::sqrt( dx * dx + dz * dz ) - fadeFrom ) / ( fadeTo - fadeFrom ) );
					float spikes = 0.0f;
					float weight = 1.0f;
					float wavelength = spacing;
					for( uint32_t octave = 0; octave < 2; ++octave )
					{
						const float n = 1.0f - std::abs( 2.0f * valueNoise( x / wavelength, z / wavelength, 97u + octave ) - 1.0f );
						spikes += weight * n * n * n;
						weight *= 0.5f;
						wavelength /= 2.3f;
					}
					m_source[i] += amplitude * spikes / 1.5f * ridge * high * fade;
				}
			}
		}
	}

	std::error_code error;
	std::filesystem::create_directories( directory, error );
	if( GpuImages::saveFloatDDS( ( directory / "height.dds" ).wstring(), m_size, m_size, m_source.data() ) &&
		( iceMax.empty() || GpuImages::saveFloatDDS( ( directory / "ice.dds" ).wstring(), iceSize, iceSize, iceMax.data() ) ) )
	{
		std::ofstream( directory / "key.txt" ) << keyText << "\n";
		LOG( "Mountain erosion: written " + directory.string() );
	}
	else
		LOG( "Mountain erosion: can`t write " + directory.string() );
}

}
