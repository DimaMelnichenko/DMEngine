#include "WaterSurface.h"
#include "StaticWater.h"
#include <algorithm>
#include "Shaders\slots.h"
#include "Logger\Logger.h"
#include "WaterRipples.h"

namespace GS
{

namespace
{

constexpr float visibleDepth = WaterField::visibleDepth;
constexpr uint32_t tileCells = 32;				// WATER_TILE в water_surface.cs / water_surface.sh
constexpr uint32_t rippleSize = 256;			// текселей карты ряби по стороне (WaterRipples)

}

bool WaterSurface::initialize( WaterField& field )
{
	m_field = &field;
	DMD3D& d3d = DMD3D::instance();
	m_tilesPerSide = ( m_field->size + tileCells - 1 ) / tileCells;
	const uint32_t tileCount = m_tilesPerSide * m_tilesPerSide;
	if( !m_surfaceShader.Initialize( "Shaders\\water_surface.cs", "mainSurface" ) ||
		!m_tilesResetShader.Initialize( "Shaders\\water_surface.cs", "mainTilesReset" ) ||
		!m_tilesShader.Initialize( "Shaders\\water_surface.cs", "mainTiles" ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterTilesBuffer ), m_tilesBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterSurfaceBuffer ), m_surfaceBuffer ) )
		return false;
	if( !WaterRipples::create( rippleSize, m_ripple, m_rippleView ) )
	{
		LOG( "Failed to create the water ripple texture" );
		return false;
	}
	d3d.setName( m_ripple, "Water ripple" );

	TextureDesc levelDesc;
	levelDesc.width = m_field->size;
	levelDesc.height = m_field->size;
	levelDesc.format = DXGI_FORMAT_R32_FLOAT;
	levelDesc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	BufferDesc boundsDesc;
	boundsDesc.size = tileCount * sizeof( DirectX::XMFLOAT2 );
	boundsDesc.stride = sizeof( DirectX::XMFLOAT2 );
	boundsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	BufferDesc listDesc;
	listDesc.size = tileCount * sizeof( uint32_t );
	listDesc.stride = sizeof( uint32_t );
	listDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	BufferDesc argsDesc;
	argsDesc.size = 32;
	argsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::indirectArgs | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	if( !d3d.createTexture( levelDesc, nullptr, m_level ) || !d3d.createStorageView( m_level, {}, m_levelUAV ) ||
		!d3d.createShaderView( m_level, {}, m_levelView ) ||
		!d3d.createBuffer( boundsDesc, nullptr, m_tileBounds ) || !d3d.createStorageView( m_tileBounds, {}, m_tileBoundsUAV ) ||
		!d3d.createShaderView( m_tileBounds, {}, m_tileBoundsView ) ||
		!d3d.createBuffer( listDesc, nullptr, m_tileList ) || !d3d.createStorageView( m_tileList, {}, m_tileListUAV ) ||
		!d3d.createShaderView( m_tileList, {}, m_tileListView ) ||
		!d3d.createBuffer( argsDesc, nullptr, m_drawArgs ) || !d3d.createStorageView( m_drawArgs, rawView, m_drawArgsUAV ) )
	{
		LOG( "Failed to create water surface resources" );
		return false;
	}
	d3d.setName( m_level, "Water surface level" );
	d3d.setName( m_tileBounds, "Water tile bounds" );
	d3d.setName( m_tileList, "Water visible tiles" );
	d3d.setName( m_drawArgs, "Water indirect command" );

	m_tileMesh.initialize( tileCells + 1, tileCells + 1 );
	m_surfaceProgram.setLayoutDesc( { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } } );
	if( !m_surfaceProgram.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\water.vs" ) ||
		!m_surfaceProgram.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\water.ps" ) ||
		!m_surfaceProgram.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\water.vs", "WATER_DETAIL" ) ||
		!m_surfaceProgram.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\water.ps", "WATER_DETAIL" ) )
	{
		LOG( "Water surface: shader compilation failed" );
		return false;
	}
	m_surfacePhase = m_surfaceProgram.createPhase( 0, 0 );
	m_detailPhase = m_surfaceProgram.createPhase( 1, 1 );

	// Детальная поверхность у русел: сетка плитки — вершина на тексель плитки с полем, свои список экземпляров и команда
	const uint32_t side = TerrainHydrology::detailTileCells * TerrainHydrology::detailSamples + 2;
	m_detailMesh.initialize( static_cast<uint16_t>( side ), static_cast<uint16_t>( side ) );
	if( !d3d.createBuffer( listDesc, nullptr, m_detailTileList ) || !d3d.createStorageView( m_detailTileList, {}, m_detailTileListUAV ) ||
		!d3d.createShaderView( m_detailTileList, {}, m_detailTileListView ) ||
		!d3d.createBuffer( argsDesc, nullptr, m_detailDrawArgs ) || !d3d.createStorageView( m_detailDrawArgs, rawView, m_detailDrawArgsUAV ) )
	{
		LOG( "Failed to create water detail surface resources" );
		return false;
	}
	d3d.setName( m_detailTileList, "Water visible detail tiles" );
	d3d.setName( m_detailDrawArgs, "Water detail indirect command" );
	return m_surfacePhase >= 0 && m_detailPhase >= 0;
}

void WaterSurface::build( const WaterSimulationSettings& settings )
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& heightMap = *m_field->terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water surface";
	pass.reads = { { &heightMap, "height map" }, { &m_field->depthView, "water depth" } };
	pass.writes = { { &m_levelUAV, "water level" }, { &m_tileBoundsUAV, "water tile bounds" } };
	d3d.beginPass( pass );
	m_field->setParameters( settings );
	WaterTilesBuffer tiles = {};
	tiles.tilesPerSide = m_tilesPerSide;
	tiles.visibleDepth = visibleDepth;
	tiles.worldSize = m_field->terrain->terrainHeight().worldSize;
	tiles.indexCount = m_tileMesh.indexCount();
	Device::updateResourceData( m_tilesBuffer, tiles );
	d3d.setConstantBuffer( 5, m_tilesBuffer );
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 3, m_field->depthView );
	d3d.setUAV( 0, m_levelUAV );
	d3d.setUAV( 1, m_tileBoundsUAV );
	m_surfaceShader.dispatchGroups( m_tilesPerSide, m_tilesPerSide, 1 );
	m_dirty = false;
}

void WaterSurface::cull( const RenderView& view, const WaterSimulationSettings& settings, const StaticWater& water )
{
	DMD3D& d3d = DMD3D::instance();
	PassDesc pass;
	pass.name = "Water tiles";
	pass.reads = { { &m_tileBoundsView, "water tile bounds" } };
	pass.writes = { { &m_drawArgsUAV, "water indirect command" }, { &m_tileListUAV, "water visible tiles" },
					{ &m_detailDrawArgsUAV, "water detail indirect command" }, { &m_detailTileListUAV, "water visible detail tiles" } };
	if( water.detailTileCount() > 0 )
		pass.reads.push_back( { &water.detailBoundsView(), "detail tile bounds" } );
	d3d.beginPass( pass );
	WaterTilesBuffer tiles = {};
	for( int i = 0; i < 6; ++i )
		DirectX::XMStoreFloat4( &tiles.planes[i], view.frustum.planes()[i] );
	tiles.tilesPerSide = m_tilesPerSide;
	tiles.visibleDepth = visibleDepth;
	tiles.worldSize = m_field->terrain->terrainHeight().worldSize;
	tiles.indexCount = m_tileMesh.indexCount();
	tiles.cameraPosition = view.position;
	tiles.detailDistance = water.detailTileCount() > 0 ? std::max( settings.detailDistance, 0.0f ) : 0.0f;
	tiles.detailIndexCount = m_detailMesh.indexCount();
	Device::updateResourceData( m_tilesBuffer, tiles );
	d3d.setConstantBuffer( 5, m_tilesBuffer );
	m_field->setParameters( settings );
	d3d.setUAV( 2, m_drawArgsUAV );
	d3d.setUAV( 4, m_detailDrawArgsUAV );
	m_tilesResetShader.dispatchGroups( 1, 1, 1 );
	d3d.setSRV( 4, m_tileBoundsView );
	if( water.detailTileCount() > 0 )
		d3d.setSRV( 6, water.detailBoundsView() );
	d3d.setUAV( 2, m_drawArgsUAV );
	d3d.setUAV( 3, m_tileListUAV );
	d3d.setUAV( 4, m_detailDrawArgsUAV );
	d3d.setUAV( 5, m_detailTileListUAV );
	const uint32_t tileCount = m_tilesPerSide * m_tilesPerSide;
	m_tilesShader.dispatchGroups( ( tileCount + 63 ) / 64, 1, 1 );
}

void WaterSurface::warmPipelines( const PassStates& states )
{
	m_surfaceProgram.warmPipelines( { { RasterState::solid, DepthState::readOnly, BlendState::alpha },
									  { RasterState::wireframe, DepthState::readOnly, BlendState::alpha } },
									states.scene, { m_surfacePhase, m_detailPhase } );
}

void WaterSurface::render( const RenderContext& context, const WaterSimulationSettings& settings, const StaticWater& water )
{
	DMD3D& d3d = DMD3D::instance();
	const TerrainHeight height = m_field->terrain->terrainHeight();
	WaterSurfaceBuffer params = {};
	params.size = m_field->size;
	params.cellSize = m_field->cellSize;
	params.worldSize = height.worldSize;
	params.tilesPerSide = m_tilesPerSide;
	const WaterSimulationSettings& current = settings;
	params.absorption = current.absorption;
	params.rippleStretch = std::max( current.rippleStretch, 1.0f );
	params.scatterColor = DirectX::XMFLOAT3( current.scatterColor.x * current.scatterStrength, current.scatterColor.y * current.scatterStrength,
											 current.scatterColor.z * current.scatterStrength );
	params.rippleScale = std::max( current.rippleScale, 0.01f );
	params.rippleStrength = current.rippleStrength;
	params.calmRipple = current.calmRipple;
	params.refraction = current.refraction;
	params.roughness = current.roughness;
	params.foamSpeed = std::max( current.foamSpeed, 0.01f );
	params.foamShear = std::max( current.foamShear, 0.01f );
	params.detailDistance = water.detailTileCount() > 0 ? std::max( current.detailDistance, 0.0f ) : 0.0f;
	params.foamSlope = std::max( current.channels.foamSlope, 0.01f );
	params.terrainHeightMultiplier = height.heightMultiplier;
	params.terrainHeightOffset = height.heightOffset;
	params.terrainDetailTile = height.detailTileSize;
	Device::updateResourceData( m_surfaceBuffer, params );

	ScopedRenderState state( context.frameRaster );
	d3d.setVertexBuffer( m_tileMesh.vertexBuffer(), sizeof( DirectX::XMFLOAT3 ) );
	d3d.setIndexBuffer( m_tileMesh.indexBuffer(), DXGI_FORMAT_R32_UINT );
	m_surfaceProgram.setPass( m_surfacePhase );
	d3d.setConstantBuffer( SLOT_CB_MATERIAL, m_surfaceBuffer );
	d3d.setSRV( 0, m_levelView );
	d3d.setSRV( 1, m_rippleView );
	d3d.setSRV( 2, water.stoneDragView() );
	if( height.heightMap )
		d3d.setSRV( 3, *height.heightMap );
	d3d.setSRV( SLOT_INSTANCE_DATA, m_tileListView );
	d3d.drawIndexedInstancedIndirectCount( m_drawArgs, 0, 1, m_drawArgs, 24 );
	// Детальные плитки у русел: своя сетка (0,25 м); уровень и течение — детальная вода (SLOT_WATER_DETAIL); грубая
	// поверхность в этих плитках отсечена в water.ps
	if( water.detailTileCount() > 0 )
	{
		d3d.setVertexBuffer( m_detailMesh.vertexBuffer(), sizeof( DirectX::XMFLOAT3 ) );
		d3d.setIndexBuffer( m_detailMesh.indexBuffer(), DXGI_FORMAT_R32_UINT );
		m_surfaceProgram.setPass( m_detailPhase );
		d3d.setConstantBuffer( SLOT_CB_MATERIAL, m_surfaceBuffer );
		d3d.setSRV( 1, m_rippleView );
		d3d.setSRV( 2, water.stoneDragView() );
		if( height.heightMap )
			d3d.setSRV( 3, *height.heightMap );
		d3d.setSRV( SLOT_INSTANCE_DATA, m_detailTileListView );
		d3d.drawIndexedInstancedIndirectCount( m_detailDrawArgs, 0, 1, m_detailDrawArgs, 24 );
	}
}

}
