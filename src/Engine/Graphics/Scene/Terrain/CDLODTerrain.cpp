#include "CDLODTerrain.h"
#include "TerrainEdits.h"
#include "D3D\TextureImages.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
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

bool CDLODTerrain::initialize( uint32_t terrainId, const std::vector<TerrainEdit>& edits )
{
	float heightMultiplier = 1.0f;
	std::string splatMap;
	if( !loadSettings( terrainId, heightMultiplier, splatMap ) )
		return false;
	m_heightMultiplier = heightMultiplier;

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
	if( !buildHeightBounds( edits, coverage ) || !createFoliageClearMask( coverage ) )
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

	prop = m_properties.insert( "Height multiplier", heightMultiplier );
	prop->setLow( 1.0f );
	prop->setHigh( 5000.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Triplanar sharpness", 8.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 32.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Height blend", 0.2f );
	prop->setLow( 0.01f );
	prop->setHigh( 1.0f );
	prop->setControlType( GUIControlType::SLIDER );

	// Второй масштаб текстур вдали (distance resampling, как в материалах UE Landscape): мелкий повтор слоёв
	// издалека складывается в сетку
	prop = m_properties.insert( "Far texture scale", 8.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 32.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Far blend start", 40.0f );
	prop->setLow( 0.0f );
	prop->setHigh( 500.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Far blend end", 120.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 1000.0f );
	prop->setControlType( GUIControlType::SLIDER );

	m_properties.insert( "Wireframe", false );
	m_properties.insert( "Show LOD", false );

	LOG( "CDLOD terrain: heightmap " + std::to_string( mapSize ) + ", LOD levels " + std::to_string( m_levelCount ) );

	m_initialized = true;

	return true;
}

TerrainHeight CDLODTerrain::terrainHeight() const
{
	TerrainHeight height;
	height.heightMap = &m_heightMap;
	height.foliageClearMask = &m_foliageClearMask;
	height.worldSize = m_worldSize;
	// Ползунок, а не m_heightMultiplier: тот обновляется в update() только у видимого террейна
	height.heightMultiplier = m_initialized ? m_properties["Height multiplier"].data<float>() : m_heightMultiplier;
	height.heightOffset = m_heightOffset;
	return height;
}

bool CDLODTerrain::loadSettings( uint32_t terrainId, float& heightMultiplier, std::string& splatMap )
{
	SQLite::Statement query( DBConnector::instance().db(), "select heightmap, splatmap, height_multiplier, height_offset, width_multiplier "
														   "from Terrain where id = :id" );
	query.bind( ":id", terrainId );

	if( !query.executeStep() )
	{
		LOG( "CDLOD terrain: no row " + std::to_string( terrainId ) + " in table Terrain" );
		return false;
	}

	m_heightMapName = query.getColumn( "heightmap" ).getString();
	splatMap = query.getColumn( "splatmap" ).getString();
	heightMultiplier = static_cast<float>( query.getColumn( "height_multiplier" ).getDouble() );
	m_heightOffset = static_cast<float>( query.getColumn( "height_offset" ).getDouble() );
	m_texelSize = static_cast<float>( query.getColumn( "width_multiplier" ).getDouble() );

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

bool CDLODTerrain::buildHeightBounds( const std::vector<TerrainEdit>& edits, TerrainEditCoverage& coverage )
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
	if( image->format != DXGI_FORMAT_R32_FLOAT )
	{
		if( FAILED( Convert( *image, DXGI_FORMAT_R32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted ) ) )
		{
			LOG( "CDLOD terrain: can`t convert heightmap to R32_FLOAT" );
			return false;
		}
		image = converted.GetImage( 0, 0, 0 );
	}

	// Правки рельефа (TerrainEdits) — на копию до мипов: их видят вершины всех уровней, узлы и расстановка
	if( !edits.empty() )
	{
		HeightField field;
		field.heights = reinterpret_cast<float*>( image->pixels );
		field.size = static_cast<uint32_t>( image->width );
		field.rowPitch = image->rowPitch / sizeof( float );
		field.texelSize = m_texelSize;
		field.heightMultiplier = m_heightMultiplier;
		field.heightOffset = m_heightOffset;
		const size_t changed = applyTerrainEdits( field, edits, &coverage );
		LOG( "CDLOD terrain: " + std::to_string( edits.size() ) + " terrain edits, texels changed: " + std::to_string( changed ) );
	}

	// Вершины уровня L читают мипы L и L + 1, поэтому вершинному шейдеру нужна карта высот с полной цепочкой мипов
	ScratchImage mipChain;
	if( FAILED( GenerateMipMaps( *image, TEX_FILTER_DEFAULT, 0, mipChain ) ) )
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
