#include "WaterFoam.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <DirectXPackedVector.h>
#include "Logger\Logger.h"
#include "D3D\TextureImages.h"
#include "Shaders\water_foam_constants.h"

namespace GS
{

namespace
{
constexpr float detailTimeStep = 0.25f;	// с: перенос за шаг — до 3 текселей детальной сетки на быстрой воде
constexpr float coarseTimeStep = 0.5f;	// с
constexpr float growthTime = 0.25f;		// с: пена у источника растёт к нему за это время
constexpr float settleLifetimes = 5.0f;	// шагов — на столько времён жизни: дальше поле не меняется (e⁻⁵)
constexpr uint32_t maxSteps = 400;
}

bool WaterFoam::initialize( WaterField& field )
{
	m_field = &field;
	const char* file = "Shaders\\water_foam.cs";
	if( !m_detailSourceShader.initialize( file, "mainDetailSource" ) || !m_detailStepShader.initialize( file, "mainDetailStep" ) ||
		!m_coarseSourceShader.initialize( file, "mainCoarseSource" ) || !m_coarseStepShader.initialize( file, "mainCoarseStep" ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( WaterFoamBuffer ), m_constants ) )
		return false;
	return createGrid( m_coarse, field.size, field.size, 1, "Water foam" );
}

bool WaterFoam::createGrid( Grid& grid, uint32_t width, uint32_t height, uint32_t slices, const char* name )
{
	DMD3D& d3d = DMD3D::instance();
	TextureDesc desc;
	desc.width = width;
	desc.height = height;
	desc.arraySize = slices;
	desc.format = DXGI_FORMAT_R16_FLOAT;
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	TextureViewDesc view;
	if( slices > 1 )
		view.kind = TextureViewDesc::Kind::texture2DArray;
	const char* parts[3] = { " source", " A", " B" };
	for( int i = 0; i < 3; ++i )
	{
		if( !d3d.createTexture( desc, nullptr, grid.textures[i] ) || !d3d.createStorageView( grid.textures[i], view, grid.uavs[i] ) ||
			!d3d.createShaderView( grid.textures[i], view, grid.views[i] ) )
		{
			LOG( std::string( "Failed to create " ) + name + parts[i] );
			return false;
		}
		d3d.setName( grid.textures[i], std::string( name ) + parts[i] );
	}
	grid.current = 1;
	return true;
}

bool WaterFoam::builtWith( const WaterSimulationSettings& settings ) const
{
	return m_ready && settings.foamSpeed == m_builtSpeed && settings.foamShear == m_builtShear &&
		   settings.channels.foamSlope == m_builtSlope && settings.foamLifetime == m_builtLifetime;
}

bool WaterFoam::build( const StaticWater& water, const WaterSimulationSettings& settings )
{
	m_ready = false;
	if( !m_field || water.flowTileCount() == 0 )
		return false;
	const auto start = std::chrono::high_resolution_clock::now();
	DMD3D& d3d = DMD3D::instance();
	const TerrainHeight height = m_field->terrain->terrainHeight();
	const bool detail = water.detailTileCount() > 0 && water.detailBlockCount() > 0 && height.detailIndex;
	if( detail && water.detailTileCount() != m_detailSlices )
	{
		// Массив — хотя бы из двух срезов: вид массива и у одной плитки
		if( !createGrid( m_detail, water.detailSide(), water.detailSide(), std::max( water.detailTileCount(), 2u ), "Water foam detail" ) )
			return false;
		m_detailSlices = water.detailTileCount();
	}
	const float lifetime = std::max( settings.foamLifetime, 0.1f );

	WaterFoamBuffer params = {};
	params.foamSpeed = std::max( settings.foamSpeed, 0.01f );
	params.foamShear = std::max( settings.foamShear, 0.01f );
	params.foamSlope = std::max( settings.channels.foamSlope, 0.01f );
	params.foamWetDepth = WaterField::visibleDepth;
	params.foamCellSize = m_field->cellSize;
	params.foamWorldSize = height.worldSize;
	params.foamTileSize = water.detailTileSize();
	params.foamSide = water.detailSide();
	params.foamGridSize = m_field->size;
	params.foamDetail = detail ? 1u : 0u;

	// Сетка: источник, затем шаги — каждый читает поле прошлого и пишет другое; итог — в grid.current
	auto solve = [&]( Grid& grid, ComputeShader& sourceShader, ComputeShader& stepShader, float timeStep, uint32_t groups,
					  const ShaderView& groupsView, uint32_t outputSlot, const char* name )
	{
		params.foamTimeStep = timeStep;
		params.foamDecay = std::exp( -timeStep / lifetime );
		params.foamGrowth = 1.0f - std::exp( -timeStep / growthTime );
		params.foamTileCount = groups;
		Device::updateResourceData( m_constants, params );
		for( StorageView& uav : grid.uavs )
			d3d.clearStorageView( uav );
		const bool detailGrid = &grid == &m_detail;
		auto pass = [&]( int read, int write, ComputeShader& shader )
		{
			PassDesc desc;
			desc.name = name;
			desc.reads = { { &m_field->stateView, "water state" }, { &groupsView, "foam groups" } };
			if( detail )
				desc.reads.insert( desc.reads.end(), { { &water.detailWaterView(), "detail water" }, { height.detailIndex, "terrain detail index" },
													   { &water.detailTileCoordsView(), "detail tile coordinates" } } );
			if( detail && !detailGrid )
				desc.reads.push_back( { &m_detail.result(), "detail foam" } );
			if( read >= 0 )
				desc.reads.push_back( { &grid.views[read], "foam" } );
			if( write != 0 )
				desc.reads.push_back( { &grid.views[0], "foam source" } );
			desc.writes = { { &grid.uavs[write], "foam" } };
			d3d.beginPass( desc );
			d3d.setConstantBuffer( 5, m_constants );
			if( detail )
			{
				d3d.setSRV( 0, water.detailWaterView() );
				d3d.setSRV( 1, *height.detailIndex );
				d3d.setSRV( 2, water.detailTileCoordsView() );
			}
			d3d.setSRV( 3, groupsView );
			d3d.setSRV( 4, m_field->stateView );
			// Детальная сетка: прошлый шаг — слот 5, источник — 7; сетка 1 м: итог детальной — 5, прошлый шаг — 6, источник — 8
			if( detailGrid )
			{
				if( read >= 0 )
					d3d.setSRV( 5, grid.views[read] );
				d3d.setSRV( 7, grid.views[0] );
			}
			else
			{
				if( detail )
					d3d.setSRV( 5, m_detail.result() );
				if( read >= 0 )
					d3d.setSRV( 6, grid.views[read] );
				d3d.setSRV( 8, grid.views[0] );
			}
			d3d.setUAV( outputSlot, grid.uavs[write] );
			shader.dispatchGroups( groups, 1, 1 );
		};
		pass( -1, 0, sourceShader );
		const uint32_t steps = std::min( static_cast<uint32_t>( std::ceil( settleLifetimes * lifetime / timeStep ) ), maxSteps );
		int read = 1;
		for( uint32_t s = 0; s < steps; ++s )
		{
			const int write = read == 1 ? 2 : 1;
			pass( read, write, stepShader );
			read = write;
		}
		grid.current = static_cast<uint32_t>( read );
		return steps;
	};
	uint32_t detailSteps = 0;
	if( detail )
		detailSteps = solve( m_detail, m_detailSourceShader, m_detailStepShader, detailTimeStep, water.detailBlockCount(),
							 water.detailBlocksView(), 0, "Water foam detail" );
	const uint32_t coarseSteps = solve( m_coarse, m_coarseSourceShader, m_coarseStepShader, coarseTimeStep, water.flowTileCount(),
										water.flowTilesView(), 1, "Water foam" );
	d3d.prepareShaderRead( m_coarse.result() );
	if( detail )
		d3d.prepareShaderRead( m_detail.result() );
	d3d.waitForGpu();
	m_ready = true;
	m_builtSpeed = settings.foamSpeed;
	m_builtShear = settings.foamShear;
	m_builtSlope = settings.channels.foamSlope;
	m_builtLifetime = settings.foamLifetime;
	const double ms = std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count();
	LOG( "Water foam: lifetime " + std::to_string( lifetime ) + " s; detail " + std::to_string( detailSteps ) + " steps over " +
		 std::to_string( detail ? water.detailBlockCount() : 0 ) + " blocks 8x8, 1 m grid " + std::to_string( coarseSteps ) +
		 " steps over " + std::to_string( water.flowTileCount() ) + " tiles; ms: " + std::to_string( ms ) );
	return true;
}

bool WaterFoam::exportState( const StaticWater& water, const std::string& prefix, std::string& reason )
{
	if( !m_ready )
		return true;
	DMD3D& d3d = DMD3D::instance();
	auto half = []( const uint8_t* data, size_t index )
	{
		return DirectX::PackedVector::XMConvertHalfToFloat( reinterpret_cast<const uint16_t*>( data )[index] );
	};
	auto save = [&]( const std::string& file, uint32_t side, const std::vector<float>& values )
	{
		if( GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), side, side, values.data() ) )
			return true;
		reason = "cannot write " + file;
		return false;
	};
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	// Сетка 1 м — строки как у текстуры (строка 0 — дальний край по z)
	if( !d3d.captureTexture( m_coarse.textures[m_coarse.current], copies, bytes ) || copies.empty() )
	{
		reason = "water foam readback failed";
		return false;
	}
	const uint32_t size = m_field->size;
	std::vector<float> coarse( static_cast<size_t>( size ) * size );
	for( uint32_t row = 0; row < size; ++row )
		for( uint32_t col = 0; col < size; ++col )
			coarse[static_cast<size_t>( row ) * size + col] = half( bytes.data() + copies[0].offset + row * copies[0].rowPitch, col );
	if( !save( prefix + "_foam.dds", size, coarse ) )
		return false;
	if( m_detailSlices == 0 || water.detailTileCount() != m_detailSlices )
	{
		LOG( "Water foam: " + prefix + "_foam.dds" );
		return true;
	}
	// Детальная — плитки в растр мира (внутренние тексели), строка 0 — дальний край по z, как StaticWater::exportDetail
	if( !d3d.captureTexture( m_detail.textures[m_detail.current], copies, bytes ) || copies.size() < m_detailSlices )
	{
		reason = "water foam readback failed";
		return false;
	}
	const uint32_t inner = water.detailSide() - 2;
	const uint32_t side = static_cast<uint32_t>( std::lround( m_field->terrain->terrainHeight().worldSize / water.detailTileSize() ) ) * inner;
	std::vector<float> detail( static_cast<size_t>( side ) * side, 0.0f );
	const std::vector<DirectX::XMUINT2>& coords = water.detailCoords();
	for( uint32_t t = 0; t < m_detailSlices && t < coords.size(); ++t )
		for( uint32_t j = 1; j <= inner; ++j )
		{
			const uint32_t z = coords[t].y * inner + j - 1;
			if( z >= side )
				continue;
			for( uint32_t i = 1; i <= inner; ++i )
			{
				const uint32_t x = coords[t].x * inner + i - 1;
				if( x < side )
					detail[static_cast<size_t>( side - 1 - z ) * side + x] = half( bytes.data() + copies[t].offset + j * copies[t].rowPitch, i );
			}
		}
	if( !save( prefix + "_detail_foam.dds", side, detail ) )
		return false;
	LOG( "Water foam: " + prefix + "_foam.dds, _detail_foam.dds" );
	return true;
}

}
