#include "StaticWater.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>
#include <DirectXPackedVector.h>
#include "Assets.h"
#include "Shaders\slots.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "Utils\Fingerprint.h"
#include "D3D\TextureImages.h"
#include "Properties\SettingsProperties.h"

namespace GS
{

namespace
{

constexpr uint32_t groupSize = WaterField::groupSize;
constexpr float gravity = WaterField::gravity;
constexpr float visibleDepth = WaterField::visibleDepth;
// Течение статичной воды (water_flow.cs)
constexpr uint32_t flowStepsPerSubmit = 50;		// шагов в одной отправке GPU при загрузке
constexpr uint32_t validationFlowSteps = 20;	// под GPU-based validation шаг в десятки раз дольше
// Детальная сетка под GPU-based validation: первый вызов переноса и вязкости слой обрабатывает минутами (их байткод велик —
// грани читают ячейки через индекс плиток), поэтому под ней — без шагов, только начало, грани и итог на первых блоках.
// Шаги проверены под валидацией 2026-10-11 — ошибок нет; после правки этих проходов — снова (validationDetailFlowSteps 2)
constexpr uint32_t validationDetailFlowSteps = 0;
constexpr uint32_t validationDetailFlowBlocks = 256;
constexpr uint32_t flowTile = 8;				// тайл счёта течения — группа потоков 8 × 8 (water_flow.cs)
constexpr float flowMinDepth = TerrainHydrology::flowMinDepth;			// м: мельче — берег (скорость 0)
constexpr float flowScreen = 1e-3f;				// экранирование давления, м: озеро без истока тоже решается
constexpr float maxViscosityShare = 0.2f;		// ν · dt / l² — не больше: явная вязкость устойчива
// Детальная сетка: шаг — доля шага сетки 1 м (ячейка вчетверо мельче, перенос за шаг — не дальше пары ячеек при 2 м/с)
constexpr float detailTimeStepShare = 0.5f;
// Кэш течения на диске (solveStaticFlow): каталог и версия расчёта — поднять при правке расчёта в C++ (константы, порядок
// проходов, состав сохранённого); правку шейдера течения ключ видит сам. Хранятся последние flowCacheFiles файлов
constexpr const char* flowCacheDirectory = "cache\\water";
constexpr uint32_t flowCacheVersion = 1;
constexpr size_t flowCacheFiles = 4;
// Камни русла в течении: высота над дном — доля поперечника (камень утоплен на четверть, Scatterer::stoneEmbed), и
// коэффициент сопротивления (обтекаемый камень — 0,5–1,2)
constexpr float stoneHeight = 0.5f;
constexpr float stoneDragCoefficient = 1.0f;
constexpr int stoneSpread = 2;	// клеток: среднее сопротивление камней — по квадрату (2 · 2 + 1)² клеток ручья вокруг
constexpr float flowRelaxation = 1.8f;			// верхняя релаксация давления: сходится быстрее Гаусса-Зейделя

// Файл кэша течения: заголовок, течение сетки 1 м (X, Z мира — half, как в текстуре воды), детальной воды (X, Z — float,
// у всех текселей плиток с полем; нет — если течения детальной сетки не было), сводки расчётов
struct FlowCacheHeader
{
	char magic[4] = { 'D', 'M', 'W', 'F' };
	uint32_t version = flowCacheVersion;
	uint32_t size = 0;
	uint32_t detailSide = 0;
	uint32_t detailSlices = 0;
	uint32_t coarseSummary = 0;		// длина строки сводки, байт
	uint32_t detailSummary = 0;
	uint32_t padding = 0;
};

}

bool StaticWater::initialize( WaterField& field, const WaterSimulationSettings& initial )
{
	m_field = &field;
	m_initial = initial;
	DMD3D& d3d = DMD3D::instance();
	return m_staticShader.initialize( "Shaders\\water_simulation.cs", "mainStatic" ) &&
		   m_detailSurfaceShader.initialize( "Shaders\\water_detail.cs", "mainDetailSurface" ) &&
		   d3d.createShaderConstantBuffer( sizeof( WaterDetailBuffer ), m_detailBuffer );
}

bool StaticWater::createTextures( const TerrainHydrology::Result& hydrology, bool enabled )
{
	m_enabled = enabled;
	m_streamManning = hydrology.streamManning;
	if( hydrology.size != m_field->size || hydrology.channelFlow.size() != static_cast<size_t>( m_field->size ) * m_field->size )
	{
		LOG( "Water simulation: water channels differ from the height map in size" );
		return false;
	}
	// Тайлы счёта течения (solveFlow): с ячейками озёр и ручьёв и восемь соседних — сухой берег вокруг
	const uint32_t tiles = ( m_field->size + flowTile - 1 ) / flowTile;
	std::vector<uint8_t> lakeTile( static_cast<size_t>( tiles ) * tiles, 0 );
	for( uint32_t row = 0; row < m_field->size; ++row )
		for( uint32_t col = 0; col < m_field->size; ++col )
		{
			const DirectX::XMFLOAT4& water = hydrology.staticWater[static_cast<size_t>( row ) * m_field->size + col];
			if( water.w > -1e8f || water.x > -1e8f )
				lakeTile[static_cast<size_t>( row / flowTile ) * tiles + col / flowTile] = 1;
		}
	m_flowTileList.clear();
	for( uint32_t ty = 0; ty < tiles; ++ty )
		for( uint32_t tx = 0; tx < tiles; ++tx )
		{
			bool adjacent = false;
			for( int dy = -1; dy <= 1 && !adjacent; ++dy )
				for( int dx = -1; dx <= 1 && !adjacent; ++dx )
				{
					const int x = static_cast<int>( tx ) + dx;
					const int y = static_cast<int>( ty ) + dy;
					adjacent = x >= 0 && y >= 0 && x < static_cast<int>( tiles ) && y < static_cast<int>( tiles ) &&
						   lakeTile[static_cast<size_t>( y ) * tiles + x];
				}
			if( adjacent )
				m_flowTileList.emplace_back( tx, ty );
		}

	DMD3D& d3d = DMD3D::instance();
	if( !m_flowTileList.empty() )
	{
		BufferDesc desc;
		desc.size = static_cast<uint32_t>( m_flowTileList.size() * sizeof( DirectX::XMUINT2 ) );
		desc.stride = sizeof( DirectX::XMUINT2 );
		desc.usage = BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( desc, m_flowTileList.data(), m_flowTiles ) || !d3d.createShaderView( m_flowTiles, {}, m_flowTilesView ) )
		{
			LOG( "Failed to create water flow tiles" );
			return false;
		}
		d3d.setName( m_flowTiles, "Water flow tiles" );
	}
	auto create = [&]( DXGI_FORMAT format, const void* data, uint32_t texelBytes, const char* name, Texture& texture, ShaderView& view )
	{
		TextureDesc desc;
		desc.width = m_field->size;
		desc.height = m_field->size;
		desc.format = format;
		TextureData initial;
		initial.data = data;
		initial.rowPitch = m_field->size * texelBytes;
		initial.slicePitch = initial.rowPitch * m_field->size;
		if( !d3d.createTexture( desc, &initial, texture ) || !d3d.createShaderView( texture, {}, view ) )
		{
			LOG( std::string( "Failed to create water texture " ) + name );
			return false;
		}
		d3d.setName( texture, name );
		return true;
	};
	// Камни русел тормозят течение (water_flow.cs, mainFlowAdvect): у клетки — площадь камней поперёк потока на квадратный
	// метр дна, только погружённая часть — до уровня воды, — и её среднее по клеткам ручья вокруг (stoneSpread)
	std::vector<float> stoneArea( static_cast<size_t>( m_field->size ) * m_field->size, 0.0f );
	const float worldSize = m_field->cellSize * m_field->size;
	for( const TerrainHydrology::Stone& stone : hydrology.stones )
	{
		const int col = static_cast<int>( stone.position.x / m_field->cellSize );
		const int row = static_cast<int>( ( worldSize - stone.position.z ) / m_field->cellSize );
		if( col < 0 || row < 0 || col >= static_cast<int>( m_field->size ) || row >= static_cast<int>( m_field->size ) )
			continue;
		const size_t index = static_cast<size_t>( row ) * m_field->size + col;
		const float level = std::max( hydrology.staticWater[index].x, hydrology.staticWater[index].w );
		const float submerged = std::clamp( level - stone.position.y, 0.0f, stoneHeight * stone.size );
		stoneArea[index] += stone.size * submerged / ( m_field->cellSize * m_field->cellSize );
	}
	std::vector<DirectX::XMFLOAT2> stoneDrag( stoneArea.size(), DirectX::XMFLOAT2( 0.0f, 0.0f ) );
	for( int row = 0; row < static_cast<int>( m_field->size ); ++row )
		for( int col = 0; col < static_cast<int>( m_field->size ); ++col )
		{
			const size_t index = static_cast<size_t>( row ) * m_field->size + col;
			if( hydrology.staticWater[index].x <= -1e8f )
				continue;
			float sum = 0.0f;
			int cells = 0;
			for( int dr = -stoneSpread; dr <= stoneSpread; ++dr )
				for( int dc = -stoneSpread; dc <= stoneSpread; ++dc )
				{
					const int r = row + dr;
					const int c = col + dc;
					if( r < 0 || c < 0 || r >= static_cast<int>( m_field->size ) || c >= static_cast<int>( m_field->size ) )
						continue;
					const size_t around = static_cast<size_t>( r ) * m_field->size + c;
					if( hydrology.staticWater[around].x <= -1e8f )
						continue;
					sum += stoneArea[around];
					++cells;
				}
			stoneDrag[index] = DirectX::XMFLOAT2( stoneArea[index], cells > 0 ? sum / cells : 0.0f );
		}
	if( !createDetailTextures( hydrology ) )
		return false;

	// Отпечаток входа течения — ключ кэша (solveStaticFlow): итоговый рельеф, растр статичной воды, камни, плитки детальной
	// земли и воды; остальное, что читает расчёт (сопротивление камней, тайлы, блоки), из них выводится
	m_flowInputKey = 0;
	const TerrainHeight height = m_field->terrain->terrainHeight();
	if( const float* heights = m_field->terrain->cpuHeights() )
	{
		Fingerprint key;
		key.add( m_field->size );
		key.add( m_field->cellSize );
		key.add( height.mapSize );
		key.add( m_streamManning );
		key.addWords( heights, static_cast<size_t>( height.mapSize ) * height.mapSize * sizeof( float ) );
		key.addWords( hydrology.staticWater.data(), hydrology.staticWater.size() * sizeof( DirectX::XMFLOAT4 ) );
		for( const TerrainHydrology::Stone& stone : hydrology.stones )
		{
			key.add( stone.position );
			key.add( stone.size );
			key.add( stone.seed );
		}
		for( const TerrainHydrology::DetailTile& tile : hydrology.detailTiles )
		{
			key.add( tile.x );
			key.add( tile.z );
			key.addWords( tile.heights.data(), tile.heights.size() * sizeof( float ) );
			key.addWords( tile.water.data(), tile.water.size() * sizeof( DirectX::XMFLOAT4 ) );
		}
		m_flowInputKey = std::max<uint64_t>( key.value, 1 );
	}
	return create( DXGI_FORMAT_R32G32B32A32_FLOAT, hydrology.staticWater.data(), sizeof( DirectX::XMFLOAT4 ), "Water static streams",
				   m_staticWaterTexture, m_staticWaterView ) &&
		   create( DXGI_FORMAT_R32G32_FLOAT, stoneDrag.data(), sizeof( DirectX::XMFLOAT2 ), "Water stone drag", m_stoneDrag, m_stoneDragView );
}

bool StaticWater::createDetailStones()
{
	// Камни русла — верх над ячейками плиток (и полем соседних): течение поднимает до него дно (water_flow.cs: не больше чем
	// на половину глубины), поле пены по нему ставит пену там, где вода над камнем тонкая (water_foam.cs).
	// Камень — полуэллипсоид: поперечник size, высота над дном stoneHeight · size (как у сопротивления сетки 1 м)
	DMD3D& d3d = DMD3D::instance();
	const uint32_t side = m_detailSide;
	const uint32_t cells = side - 2;
	const float step = m_detailTileSize / cells;
	const size_t texels = static_cast<size_t>( side ) * side;
	std::vector<float> stones( texels * m_detailTileCount, -1e9f );
	const int tilesPerSide = static_cast<int>( m_detailTilesPerSide );
	const int tileCells = static_cast<int>( cells );
	for( const TerrainHydrology::Stone& stone : m_detailStones )
	{
		const float radius = 0.5f * stone.size;
		const float rise = stoneHeight * stone.size;
		if( radius <= 0.0f )
			continue;
		const int x0 = static_cast<int>( std::floor( ( stone.position.x - radius ) / step ) );
		const int x1 = static_cast<int>( std::floor( ( stone.position.x + radius ) / step ) );
		const int z0 = static_cast<int>( std::floor( ( stone.position.z - radius ) / step ) );
		const int z1 = static_cast<int>( std::floor( ( stone.position.z + radius ) / step ) );
		for( int z = std::max( z0, 0 ); z <= z1; ++z )
			for( int x = std::max( x0, 0 ); x <= x1; ++x )
			{
				const float dx = ( x + 0.5f ) * step - stone.position.x;
				const float dz = ( z + 0.5f ) * step - stone.position.z;
				const float share = ( dx * dx + dz * dz ) / ( radius * radius );
				if( share >= 1.0f )
					continue;
				const float top = stone.position.y + rise * std::sqrt( 1.0f - share );
				// Своя плитка и поле соседних
				const int tx = x / tileCells;
				const int tz = z / tileCells;
				for( int k = 0; k < 9; ++k )
				{
					const int nx = tx + k % 3 - 1;
					const int nz = tz + k / 3 - 1;
					if( nx < 0 || nz < 0 || nx >= tilesPerSide || nz >= tilesPerSide )
						continue;
					const int slice = m_detailSliceOf[static_cast<size_t>( nz ) * tilesPerSide + nx];
					const int lx = x - nx * tileCells;
					const int lz = z - nz * tileCells;
					if( slice < 0 || lx < -1 || lz < -1 || lx > tileCells || lz > tileCells )
						continue;
					float& texel = stones[static_cast<size_t>( slice ) * texels + static_cast<size_t>( lz + 1 ) * side + lx + 1];
					texel = std::max( texel, top );
				}
			}
	}

	TextureDesc desc;
	desc.width = side;
	desc.height = side;
	desc.arraySize = m_detailTileCount;
	desc.format = DXGI_FORMAT_R32_FLOAT;
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	std::vector<TextureData> slices( m_detailTileCount );
	for( uint32_t t = 0; t < m_detailTileCount; ++t )
	{
		slices[t].data = stones.data() + t * texels;
		slices[t].rowPitch = side * static_cast<uint32_t>( sizeof( float ) );
		slices[t].slicePitch = slices[t].rowPitch * side;
	}
	if( !d3d.createTexture( desc, slices.data(), m_detailStoneTops ) || !d3d.createShaderView( m_detailStoneTops, arrayView, m_detailStonesView ) )
	{
		LOG( "Failed to create water detail stones" );
		return false;
	}
	d3d.setName( m_detailStoneTops, "Water detail stones" );
	return true;
}

bool StaticWater::createDetailTextures( const TerrainHydrology::Result& hydrology )
{
	// Детальная вода — только у статичной: срез — плитка детальной земли (тот же порядок, что у TerrainPipeline)
	m_detailTileCount = 0;
	const uint32_t side = TerrainHydrology::detailTileCells * TerrainHydrology::detailSamples + 2;
	const size_t texels = static_cast<size_t>( side ) * side;
	if( !m_enabled || hydrology.detailTiles.empty() )
		return true;
	for( const TerrainHydrology::DetailTile& tile : hydrology.detailTiles )
		if( tile.water.size() != texels )
		{
			LOG( "Water simulation: detail water differs from the detail tiles in size, no detail water" );
			return true;
		}
	DMD3D& d3d = DMD3D::instance();
	const uint32_t count = static_cast<uint32_t>( hydrology.detailTiles.size() );
	std::vector<TextureData> slices( count );
	for( uint32_t t = 0; t < count; ++t )
	{
		slices[t].data = hydrology.detailTiles[t].water.data();
		slices[t].rowPitch = side * static_cast<uint32_t>( sizeof( DirectX::XMFLOAT4 ) );
		slices[t].slicePitch = slices[t].rowPitch * side;
	}
	TextureDesc desc;
	desc.width = side;
	desc.height = side;
	desc.arraySize = count;
	desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	BufferDesc boundsDesc;
	boundsDesc.size = count * 2 * static_cast<uint32_t>( sizeof( uint32_t ) );
	boundsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	TextureDesc target = desc;
	target.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	std::vector<DirectX::XMUINT2>& coords = m_detailCoords;
	coords.clear();
	for( const TerrainHydrology::DetailTile& tile : hydrology.detailTiles )
		coords.emplace_back( tile.x, tile.z );
	BufferDesc coordsDesc;
	coordsDesc.size = count * static_cast<uint32_t>( sizeof( DirectX::XMUINT2 ) );
	coordsDesc.stride = sizeof( DirectX::XMUINT2 );
	coordsDesc.usage = BufferUsage::shaderResource | BufferUsage::structured;
	if( !d3d.createBuffer( coordsDesc, coords.data(), m_detailTileCoords ) || !d3d.createShaderView( m_detailTileCoords, {}, m_detailTileCoordsView ) ||
		!d3d.createTexture( desc, slices.data(), m_detailStatic ) || !d3d.createShaderView( m_detailStatic, arrayView, m_detailStaticView ) ||
		!d3d.createTexture( target, nullptr, m_detailWater ) || !d3d.createStorageView( m_detailWater, arrayView, m_detailWaterUAV ) ||
		!d3d.createShaderView( m_detailWater, arrayView, m_detailWaterView ) ||
		!d3d.createBuffer( boundsDesc, nullptr, m_detailBounds ) || !d3d.createStorageView( m_detailBounds, rawView, m_detailBoundsUAV ) ||
		!d3d.createShaderView( m_detailBounds, rawView, m_detailBoundsView ) )
	{
		LOG( "Failed to create water detail tiles" );
		return false;
	}
	d3d.setName( m_detailStatic, "Water detail static" );
	d3d.setName( m_detailWater, "Water detail tiles" );
	d3d.setName( m_detailBounds, "Water detail tile bounds" );

	// Течение детальной сетки (solveDetailFlow): блоки 8 × 8 ячеек, где в блоке или в текселе вокруг есть вода (сухой берег
	// у воды — тоже в счёте), плитка мира → срез, камни русла
	const uint32_t cells = side - 2;
	const uint32_t blocksPerSide = cells / flowTile;
	m_detailBlockList.clear();
	for( uint32_t t = 0; t < count; ++t )
	{
		const TerrainHydrology::DetailTile& tile = hydrology.detailTiles[t];
		for( uint32_t by = 0; by < blocksPerSide; ++by )
			for( uint32_t bx = 0; bx < blocksPerSide; ++bx )
			{
				bool wet = false;
				// Блок — тексели bx · 8 + 1 … bx · 8 + 8 (поле — тексель 0), с текселем вокруг
				for( uint32_t j = by * flowTile; j <= by * flowTile + flowTile + 1 && !wet; ++j )
					for( uint32_t i = bx * flowTile; i <= bx * flowTile + flowTile + 1 && !wet; ++i )
					{
						const size_t index = static_cast<size_t>( j ) * side + i;
						wet = tile.water[index].x > -1e8f && tile.water[index].x > tile.heights[index];
					}
				if( wet )
					m_detailBlockList.emplace_back( t, by * blocksPerSide + bx );
			}
	}
	m_detailTilesPerSide = static_cast<uint32_t>( std::lround( static_cast<float>( m_field->size ) / TerrainHydrology::detailTileCells ) );
	m_detailSliceOf.assign( static_cast<size_t>( m_detailTilesPerSide ) * m_detailTilesPerSide, -1 );
	for( uint32_t t = 0; t < count; ++t )
		if( coords[t].x < m_detailTilesPerSide && coords[t].y < m_detailTilesPerSide )
			m_detailSliceOf[static_cast<size_t>( coords[t].y ) * m_detailTilesPerSide + coords[t].x] = static_cast<int32_t>( t );
	// Мокрые блоки — по сетке мира: окну подробного течения у камеры (detailWaterIn)
	m_detailBlocksPerSide = m_detailTilesPerSide * blocksPerSide;
	m_detailWetBlocks.assign( static_cast<size_t>( m_detailBlocksPerSide ) * m_detailBlocksPerSide, 0 );
	for( const DirectX::XMUINT2& block : m_detailBlockList )
	{
		const uint32_t x = coords[block.x].x * blocksPerSide + block.y % blocksPerSide;
		const uint32_t z = coords[block.x].y * blocksPerSide + block.y / blocksPerSide;
		if( x < m_detailBlocksPerSide && z < m_detailBlocksPerSide )
			m_detailWetBlocks[static_cast<size_t>( z ) * m_detailBlocksPerSide + x] = 1;
	}
	m_detailStones = hydrology.stones;
	if( !m_detailBlockList.empty() )
	{
		BufferDesc blocksDesc;
		blocksDesc.size = static_cast<uint32_t>( m_detailBlockList.size() * sizeof( DirectX::XMUINT2 ) );
		blocksDesc.stride = sizeof( DirectX::XMUINT2 );
		blocksDesc.usage = BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( blocksDesc, m_detailBlockList.data(), m_detailBlocks ) ||
			!d3d.createShaderView( m_detailBlocks, {}, m_detailBlocksView ) )
		{
			LOG( "Failed to create water detail flow blocks" );
			return false;
		}
		d3d.setName( m_detailBlocks, "Water detail flow blocks" );
	}
	// Без начальных данных память не обнулена: первая операция — очистка
	d3d.clearStorageView( m_detailWaterUAV );
	m_detailTileCount = count;
	m_detailSide = side;
	m_detailTileSize = TerrainHydrology::detailTileCells * m_field->cellSize;
	if( !createDetailStones() )
		return false;
	LOG( "Water detail tiles " + std::to_string( count ) + " (" + std::to_string( side ) + "x" + std::to_string( side ) + ", " +
		 std::to_string( 2 * texels * count * sizeof( DirectX::XMFLOAT4 ) / 1024 / 1024 ) + " MB)" );
	return true;
}

bool StaticWater::build( const WaterSimulationSettings& settings )
{
	const ShaderView& staticWater = m_staticWaterView;
	DMD3D& d3d = DMD3D::instance();
	m_field->setParameters( settings );
	const ShaderView& heightMap = *m_field->terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water static";
	pass.reads = { { &heightMap, "height map" }, { &staticWater, "static water" } };
	pass.writes = { { &m_field->depthUAV, "water depth" }, { &m_field->stateUAV, "water state" } };
	d3d.beginPass( pass );
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 3, staticWater );
	d3d.setUAV( 0, m_field->depthUAV );
	d3d.setUAV( 2, m_field->stateUAV );
	const uint32_t groups = ( m_field->size + groupSize - 1 ) / groupSize;
	m_staticShader.dispatchGroups( groups, groups, 1 );
	return true;
}

bool StaticWater::createFlowResources()
{
	DMD3D& d3d = DMD3D::instance();
	// Воспроизводимо (-O3 и строгий IEEE в Debug и Release, ShaderCompiler::compile): течение одно в обеих конфигурациях, а
	// неоптимизированный байткод переноса и вязкости детальной сетки под GPU-based validation при первом вызове
	// обрабатывался минутами (~170 с каждый)
	const char* file = "Shaders\\water_flow.cs";
	const bool same = true;
	if( !m_flowInitShader.initialize( file, "mainFlowInit", same ) || !m_flowFacesShader.initialize( file, "mainFlowFaces", same ) ||
		!m_flowAdvectShader.initialize( file, "mainFlowAdvect", same ) ||
		!m_flowViscosityShader.initialize( file, "mainFlowViscosity", same ) ||
		!m_flowDivergenceShader.initialize( file, "mainFlowDivergence", same ) ||
		!m_flowRedShader.initialize( file, "mainFlowRed", same ) || !m_flowBlackShader.initialize( file, "mainFlowBlack", same ) ||
		!m_flowProjectShader.initialize( file, "mainFlowProject", same ) ||
		!m_flowOutputShader.initialize( file, "mainFlowOutput", same ) || !m_flowRestoreShader.initialize( file, "mainFlowRestore", same ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterFlowBuffer ), m_flowBuffer ) )
		return false;
	// Детальная сетка — тот же шейдер с FLOW_DETAIL
	const char* detail = "FLOW_DETAIL";
	if( !m_detailFlowInitShader.initialize( file, "mainFlowInit", same, detail ) ||
		!m_detailFlowFacesShader.initialize( file, "mainFlowFaces", same, detail ) ||
		!m_detailFlowAdvectShader.initialize( file, "mainFlowAdvect", same, detail ) ||
		!m_detailFlowViscosityShader.initialize( file, "mainFlowViscosity", same, detail ) ||
		!m_detailFlowNudgeShader.initialize( file, "mainFlowNudge", same, detail ) ||
		!m_detailFlowDivergenceShader.initialize( file, "mainFlowDivergence", same, detail ) ||
		!m_detailFlowRedShader.initialize( file, "mainFlowRed", same, detail ) ||
		!m_detailFlowBlackShader.initialize( file, "mainFlowBlack", same, detail ) ||
		!m_detailFlowProjectShader.initialize( file, "mainFlowProject", same, detail ) ||
		!m_detailFlowOutputShader.initialize( file, "mainFlowOutput", same, detail ) ||
		!m_detailFlowRestoreShader.initialize( file, "mainFlowRestore", same, detail ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterDetailFlowBuffer ), m_detailFlowBuffer ) )
		return false;
	for( auto [texture, uav, format, name] :
		 { std::tuple{ &m_flowInfo, &m_flowInfoUAV, DXGI_FORMAT_R32G32B32A32_FLOAT, "Water flow cells" },
		   std::tuple{ &m_flowVelocity, &m_flowVelocityUAV, DXGI_FORMAT_R32G32_FLOAT, "Water flow velocity" },
		   std::tuple{ &m_flowAdvected, &m_flowAdvectedUAV, DXGI_FORMAT_R32G32_FLOAT, "Water flow advected" },
		   std::tuple{ &m_flowPressure, &m_flowPressureUAV, DXGI_FORMAT_R32_FLOAT, "Water flow pressure" },
		   std::tuple{ &m_flowDivergence, &m_flowDivergenceUAV, DXGI_FORMAT_R32_FLOAT, "Water flow divergence" } } )
	{
		TextureDesc desc;
		desc.width = m_field->size;
		desc.height = m_field->size;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) )
		{
			LOG( std::string( "Failed to create water flow texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
	}
	return true;
}

std::string StaticWater::solveFlow( const Flow& flow )
{
	m_flowBuilt = flow;
	if( !m_enabled || m_flowTileList.empty() )
		return {};
	const auto start = std::chrono::high_resolution_clock::now();
	DMD3D& d3d = DMD3D::instance();
	const float timeStep = std::max( flow.timeStep, 0.01f );
	uint32_t steps = flow.settleTime > 0.0f ? static_cast<uint32_t>( std::ceil( flow.settleTime / timeStep ) ) : 0;
	if( d3d.gpuValidation() && steps > validationFlowSteps )
	{
		LOG( "Water flow: " + std::to_string( steps ) + " steps are cut to " + std::to_string( validationFlowSteps ) +
			 " under GPU-based validation" );
		steps = validationFlowSteps;
	}
	const uint32_t iterations = static_cast<uint32_t>( std::clamp( flow.iterations, 1, 200 ) );

	WaterFlowBuffer params = flowParameters( flow, flow.viscosity, timeStep, m_field->cellSize, static_cast<uint32_t>( m_flowTileList.size() ) );
	params.flowStoneDrag = timeStep * 0.5f * stoneDragCoefficient;

	// Тайлы прошлого расчёта (другие русла) — сухие: соседи за краем тайлов читаются нулями
	for( StorageView* uav : { &m_flowInfoUAV, &m_flowVelocityUAV, &m_flowAdvectedUAV, &m_flowPressureUAV, &m_flowDivergenceUAV } )
		d3d.clearStorageView( *uav );
	const ShaderView& heightMap = *m_field->terrain->terrainHeight().heightMap;
	auto begin = [&]()
	{
		PassDesc pass;
		pass.name = "Water flow";
		pass.reads = { { &heightMap, "height map" }, { &m_staticWaterView, "static water" }, { &m_flowTilesView, "flow tiles" },
					   { &m_stoneDragView, "stone drag" } };
		pass.writes = { { &m_flowInfoUAV, "flow cells" },	   { &m_flowVelocityUAV, "flow velocity" },
						{ &m_flowAdvectedUAV, "flow advected" }, { &m_flowPressureUAV, "flow pressure" },
						{ &m_flowDivergenceUAV, "flow divergence" }, { &m_field->stateUAV, "water state" } };
		d3d.beginPass( pass );
		m_field->setParameters( m_initial );
		Device::updateResourceData( m_flowBuffer, params );
		d3d.setConstantBuffer( 5, m_flowBuffer );
		d3d.setSRV( 0, heightMap );
		d3d.setSRV( 3, m_staticWaterView );
		d3d.setSRV( 5, m_flowTilesView );
		d3d.setSRV( 6, m_stoneDragView );
	};
	// Привязка UAV после dispatch ставит барьер UAV → UAV: следующий проход видит записанное предыдущим
	const uint32_t groups = static_cast<uint32_t>( m_flowTileList.size() );
	auto dispatch = [&]( ComputeShader& shader )
	{
		d3d.setUAV( 0, m_flowInfoUAV );
		d3d.setUAV( 1, m_flowVelocityUAV );
		d3d.setUAV( 2, m_flowAdvectedUAV );
		d3d.setUAV( 3, m_flowPressureUAV );
		d3d.setUAV( 4, m_flowDivergenceUAV );
		d3d.setUAV( 5, m_field->stateUAV );
		shader.dispatchGroups( groups, 1, 1 );
	};
	// Порция шагов — шаг прогресса загрузки: после ожидания GPU проход и привязки ставит begin. Под GPU-based validation
	// шаг — ~0,5 с, порции мельче: экран загрузки обновляется, окно не «зависает»
	const uint32_t stepsPerSubmit = d3d.gpuValidation() ? 2 : flowStepsPerSubmit;
	SlowTask task( static_cast<float>( std::max( steps, 1u ) ) );
	begin();
	dispatch( m_flowInitShader );
	dispatch( m_flowFacesShader );
	for( uint32_t step = 0; step < steps; ++step )
	{
		if( step > 0 && step % stepsPerSubmit == 0 )
		{
			d3d.waitForGpu();
			task.enterProgressFrame( static_cast<float>( stepsPerSubmit ) );
			begin();
		}
		dispatch( m_flowAdvectShader );
		dispatch( m_flowViscosityShader );
		dispatch( m_flowDivergenceShader );
		for( uint32_t i = 0; i < iterations; ++i )
		{
			dispatch( m_flowRedShader );
			dispatch( m_flowBlackShader );
		}
		dispatch( m_flowProjectShader );
	}
	dispatch( m_flowOutputShader );
	d3d.waitForGpu();

	// Сводка: ячеек озёр и ручьёв, средняя и наибольшая скорость — установилось ли течение (сравнить с расчётом подольше);
	// у ручьёв — насколько поле отошло от скорости по Маннингу u₀ (у слияний, изгибов, впадений)
	std::vector<DMD3D::SubresourceCopy> infoCopies, velocityCopies;
	std::vector<uint8_t> infoBytes, velocityBytes;
	double lakeSum = 0.0, streamSum = 0.0, baseSum = 0.0, deviationSum = 0.0;
	float lakePeak = 0.0f, streamPeak = 0.0f;
	uint32_t lakeCells = 0, streamCells = 0;
	if( d3d.captureTexture( m_flowInfo, infoCopies, infoBytes ) && d3d.captureTexture( m_flowVelocity, velocityCopies, velocityBytes ) &&
		!infoCopies.empty() && !velocityCopies.empty() )
	{
		for( uint32_t row = 0; row < m_field->size; ++row )
		{
			const float* info = reinterpret_cast<const float*>( infoBytes.data() + infoCopies[0].offset + row * infoCopies[0].rowPitch );
			const float* velocity =
				reinterpret_cast<const float*>( velocityBytes.data() + velocityCopies[0].offset + row * velocityCopies[0].rowPitch );
			const float* above = row > 0 ? reinterpret_cast<const float*>( velocityBytes.data() + velocityCopies[0].offset +
																		   ( row - 1 ) * velocityCopies[0].rowPitch ) :
										   nullptr;
			for( uint32_t col = 0; col < m_field->size; ++col )
			{
				const float kind = info[col * 4 + 2];
				if( kind != 1.0f && kind != 2.0f )
					continue;
				// Скорость ячейки — средняя её открытых граней (скорость хранится на восточной и южной грани; грань открыта,
				// если сосед мокрый — как mainFlowOutput)
				auto wet = [&]( int64_t r, int64_t c )
				{
					if( r < 0 || c < 0 || r >= m_field->size || c >= m_field->size )
						return false;
					const float* line = reinterpret_cast<const float*>( infoBytes.data() + infoCopies[0].offset + r * infoCopies[0].rowPitch );
					return line[c * 4 + 2] != 0.0f;
				};
				auto average = [&]( bool first, float a, bool second, float b )
				{
					const int open = ( first ? 1 : 0 ) + ( second ? 1 : 0 );
					return open ? ( ( first ? a : 0.0f ) + ( second ? b : 0.0f ) ) / open : 0.0f;
				};
				const float x = average( wet( row, col + 1 ), velocity[col * 2], wet( row, static_cast<int64_t>( col ) - 1 ), col > 0 ? velocity[col * 2 - 2] : 0.0f );
				const float y = average( wet( row + 1, col ), velocity[col * 2 + 1], wet( static_cast<int64_t>( row ) - 1, col ), above ? above[col * 2 + 1] : 0.0f );
				const float speed = std::hypot( x, y );
				if( kind == 1.0f )
				{
					lakeSum += speed;
					lakePeak = std::max( lakePeak, speed );
					++lakeCells;
				}
				else if( info[col * 4 + 3] >= flowMinDepth )
				{
					streamSum += speed;
					streamPeak = std::max( streamPeak, speed );
					baseSum += std::hypot( info[col * 4], info[col * 4 + 1] );
					deviationSum += std::hypot( x - info[col * 4], y - info[col * 4 + 1] );
					++streamCells;
				}
			}
		}
	}
	char text[320];
	snprintf( text, sizeof( text ),
			  "Water flow: %u steps x %u iterations over %zu tiles 8x8; lake cells %u, mean speed %.3f m/s, max %.3f m/s; stream cells %u, "
			  "mean speed %.3f m/s (Manning %.3f), max %.3f m/s, off Manning %.0f%%, ms: %.1f",
			  steps, iterations, m_flowTileList.size(), lakeCells, lakeCells ? lakeSum / lakeCells : 0.0, lakePeak, streamCells,
			  streamCells ? streamSum / streamCells : 0.0, streamCells ? baseSum / streamCells : 0.0, streamPeak,
			  baseSum > 0.0 ? 100.0 * deviationSum / baseSum : 0.0,
			  std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count() );
	LOG( text );
	return text;
}

WaterFlowBuffer StaticWater::flowParameters( const Flow& flow, float viscosity, float timeStep, float cell, uint32_t groups ) const
{
	WaterFlowBuffer params = {};
	params.flowTileCount = groups;
	params.flowTimeStep = timeStep;
	params.flowViscosity = std::min( viscosity * timeStep / ( cell * cell ), maxViscosityShare );
	params.flowFriction = timeStep * gravity * flow.manning * flow.manning;
	params.flowStreamFriction = timeStep * gravity * m_streamManning * m_streamManning;
	params.flowMinDepth = flowMinDepth;
	params.flowLayer = std::max( flow.layer, flowMinDepth );
	params.flowScreen = flowScreen;
	params.flowRelaxation = flowRelaxation;
	return params;
}

bool StaticWater::detailWaterIn( float minX, float minZ, float maxX, float maxZ ) const
{
	if( m_detailBlocksPerSide == 0 || m_detailTileSize <= 0.0f )
		return false;
	// Блок — плитка / блоков на плитку по стороне
	const float block = m_detailTileSize * m_detailTilesPerSide / m_detailBlocksPerSide;
	const int last = static_cast<int>( m_detailBlocksPerSide ) - 1;
	const int x0 = std::clamp( static_cast<int>( std::floor( minX / block ) ), 0, last );
	const int x1 = std::clamp( static_cast<int>( std::floor( maxX / block ) ), 0, last );
	const int z0 = std::clamp( static_cast<int>( std::floor( minZ / block ) ), 0, last );
	const int z1 = std::clamp( static_cast<int>( std::floor( maxZ / block ) ), 0, last );
	for( int z = z0; z <= z1; ++z )
		for( int x = x0; x <= x1; ++x )
			if( m_detailWetBlocks[static_cast<size_t>( z ) * m_detailBlocksPerSide + x] )
				return true;
	return false;
}

WaterDetailFlowBuffer StaticWater::detailFlowParameters() const
{
	WaterDetailFlowBuffer params = {};
	params.detailCells = m_detailSide > 2 ? m_detailSide - 2 : 0;
	params.detailTilesPerSide = m_detailTilesPerSide;
	params.detailStep = params.detailCells > 0 ? m_detailTileSize / params.detailCells : 0.0f;
	params.detailWetDepth = flowMinDepth;
	return params;
}

std::string StaticWater::solveDetailFlow( const Flow& flow )
{
	if( !m_enabled || m_detailTileCount == 0 || m_detailBlockList.empty() || flow.detailSettleTime <= 0.0f )
		return {};
	const TerrainHeight height = m_field->terrain->terrainHeight();
	if( !height.detailTiles || !height.detailIndex )
		return {};
	const auto start = std::chrono::high_resolution_clock::now();
	DMD3D& d3d = DMD3D::instance();
	const uint32_t side = m_detailSide;
	const uint32_t cells = side - 2;
	const float step = m_detailTileSize / cells;
	const float timeStep = std::max( flow.timeStep, 0.01f ) * detailTimeStepShare;
	uint32_t steps = static_cast<uint32_t>( std::ceil( flow.detailSettleTime / timeStep ) );
	uint32_t blocks = static_cast<uint32_t>( m_detailBlockList.size() );
	if( d3d.gpuValidation() )
	{
		LOG( "Water detail flow: " + std::to_string( steps ) + " steps over " + std::to_string( blocks ) + " blocks are cut to " +
			 std::to_string( std::min( steps, validationDetailFlowSteps ) ) + " over " +
			 std::to_string( std::min( blocks, validationDetailFlowBlocks ) ) + " under GPU-based validation" );
		steps = std::min( steps, validationDetailFlowSteps );
		blocks = std::min( blocks, validationDetailFlowBlocks );
	}
	const uint32_t iterations = static_cast<uint32_t>( std::clamp( flow.iterations, 1, 200 ) );

	// Массивы счёта — на время расчёта: срез на плитку, тексели — как у детальной воды (с полем)
	TextureDesc desc;
	desc.width = side;
	desc.height = side;
	desc.arraySize = m_detailTileCount;
	desc.usage = TextureUsage::unorderedAccess;
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	Texture info, info2, velocity, advected, pressure, divergence;
	StorageView infoUAV, info2UAV, velocityUAV, advectedUAV, pressureUAV, divergenceUAV;
	for( auto [texture, uav, format, name] :
		 { std::tuple{ &info, &infoUAV, DXGI_FORMAT_R32G32B32A32_FLOAT, "Water detail flow cells" },
		   std::tuple{ &info2, &info2UAV, DXGI_FORMAT_R32G32_FLOAT, "Water detail flow radius" },
		   std::tuple{ &velocity, &velocityUAV, DXGI_FORMAT_R32G32_FLOAT, "Water detail flow velocity" },
		   std::tuple{ &advected, &advectedUAV, DXGI_FORMAT_R32G32_FLOAT, "Water detail flow advected" },
		   std::tuple{ &pressure, &pressureUAV, DXGI_FORMAT_R32_FLOAT, "Water detail flow pressure" },
		   std::tuple{ &divergence, &divergenceUAV, DXGI_FORMAT_R32_FLOAT, "Water detail flow divergence" } } )
	{
		desc.format = format;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, arrayView, *uav ) )
		{
			LOG( std::string( "Failed to create water detail flow texture " ) + name );
			return {};
		}
		d3d.setName( *texture, name );
	}

	const WaterFlowBuffer params = flowParameters( flow, flow.viscosity, timeStep, step, blocks );
	const WaterDetailFlowBuffer detailParams = detailFlowParameters();

	// Блоки прошлого расчёта и ячейки вне блоков — сухие
	for( StorageView* uav : { &infoUAV, &info2UAV, &velocityUAV, &advectedUAV, &pressureUAV, &divergenceUAV } )
		d3d.clearStorageView( *uav );
	auto begin = [&]()
	{
		PassDesc pass;
		pass.name = "Water detail flow";
		pass.reads = { { &m_detailStaticView, "detail static water" }, { height.detailTiles, "terrain detail tiles" },
					   { height.detailIndex, "terrain detail index" }, { &m_detailTileCoordsView, "detail tile coordinates" },
					   { &m_detailBlocksView, "detail flow blocks" },	{ &m_detailStonesView, "detail stones" },
					   { &m_field->stateView, "water state" } };
		pass.writes = { { &infoUAV, "detail flow cells" },		   { &info2UAV, "detail flow radius" },
						{ &velocityUAV, "detail flow velocity" },	   { &advectedUAV, "detail flow advected" },
						{ &pressureUAV, "detail flow pressure" },	   { &divergenceUAV, "detail flow divergence" },
						{ &m_detailWaterUAV, "detail water" } };
		d3d.beginPass( pass );
		m_field->setParameters( m_initial );
		Device::updateResourceData( m_flowBuffer, params );
		d3d.setConstantBuffer( 5, m_flowBuffer );
		Device::updateResourceData( m_detailFlowBuffer, detailParams );
		d3d.setConstantBuffer( 6, m_detailFlowBuffer );
		d3d.setSRV( 3, m_detailStaticView );
		d3d.setSRV( 5, m_detailBlocksView );
		d3d.setSRV( 6, *height.detailTiles );
		d3d.setSRV( 7, *height.detailIndex );
		d3d.setSRV( 8, m_detailTileCoordsView );
		d3d.setSRV( 9, m_detailStonesView );
		d3d.setSRV( 10, m_field->stateView );
	};
	const uint32_t groups = blocks;
	auto dispatch = [&]( ComputeShader& shader )
	{
		d3d.setUAV( 0, infoUAV );
		d3d.setUAV( 1, velocityUAV );
		d3d.setUAV( 2, advectedUAV );
		d3d.setUAV( 3, pressureUAV );
		d3d.setUAV( 4, divergenceUAV );
		d3d.setUAV( 5, m_detailWaterUAV );
		d3d.setUAV( 6, info2UAV );
		shader.dispatchGroups( groups, 1, 1 );
	};
	const uint32_t stepsPerSubmit = d3d.gpuValidation() ? 2 : flowStepsPerSubmit;
	SlowTask task( static_cast<float>( std::max( steps, 1u ) ) );
	begin();
	dispatch( m_detailFlowInitShader );
	dispatch( m_detailFlowFacesShader );
	for( uint32_t done = 0; done < steps; ++done )
	{
		if( done > 0 && done % stepsPerSubmit == 0 )
		{
			d3d.waitForGpu();
			task.enterProgressFrame( static_cast<float>( stepsPerSubmit ) );
			begin();
		}
		dispatch( m_detailFlowAdvectShader );
		dispatch( m_detailFlowViscosityShader );
		dispatch( m_detailFlowNudgeShader );
		dispatch( m_detailFlowDivergenceShader );
		for( uint32_t i = 0; i < iterations; ++i )
		{
			dispatch( m_detailFlowRedShader );
			dispatch( m_detailFlowBlackShader );
		}
		dispatch( m_detailFlowProjectShader );
	}
	dispatch( m_detailFlowOutputShader );
	d3d.waitForGpu();
	const double solveMs = std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count();

	// Сводка по ячейкам плиток (без поля): ручьи — средняя по глубине против сетки 1 м в тех же точках (билинейно по мокрым
	// клеткам, как берёт её детальный расчёт) и против u₀, озёра
	std::vector<DMD3D::SubresourceCopy> infoCopies, waterCopies, coarseCopies;
	std::vector<uint8_t> infoBytes, waterBytes, coarseBytes;
	double streamSum = 0.0, coarseSum = 0.0, baseSum = 0.0, streamDepth = 0.0, lakeSum = 0.0;
	float peak = 0.0f;
	uint32_t streamCells = 0, lakeCells = 0;
	if( d3d.captureTexture( info, infoCopies, infoBytes ) && d3d.captureTexture( m_detailWater, waterCopies, waterBytes ) &&
		d3d.captureTexture( m_field->state, coarseCopies, coarseBytes ) && infoCopies.size() >= m_detailTileCount &&
		waterCopies.size() >= m_detailTileCount && !coarseCopies.empty() )
	{
		// m_field->state — R16G16B16A16_FLOAT: глубина, течение X, Z мира
		auto coarse = [&]( int col, int row, int channel )
		{
			const uint16_t* line = reinterpret_cast<const uint16_t*>( coarseBytes.data() + coarseCopies[0].offset + row * coarseCopies[0].rowPitch );
			return DirectX::PackedVector::XMConvertHalfToFloat( line[col * 4 + channel] );
		};
		const float worldSize = m_field->size * m_field->cellSize;
		auto coarseSpeed = [&]( float x, float z )
		{
			const float gx = x / m_field->cellSize - 0.5f;
			const float gy = ( worldSize - z ) / m_field->cellSize - 0.5f;
			const int c0 = static_cast<int>( std::floor( gx ) );
			const int r0 = static_cast<int>( std::floor( gy ) );
			float vx = 0.0f, vz = 0.0f, weights = 0.0f;
			for( int k = 0; k < 4; ++k )
			{
				const int c = c0 + ( k & 1 );
				const int r = r0 + ( k >> 1 );
				const float weight = ( k & 1 ? gx - c0 : 1.0f - ( gx - c0 ) ) * ( k >> 1 ? gy - r0 : 1.0f - ( gy - r0 ) );
				if( weight <= 0.0f || c < 0 || r < 0 || c >= static_cast<int>( m_field->size ) || r >= static_cast<int>( m_field->size ) ||
					coarse( c, r, 0 ) < flowMinDepth )
					continue;
				vx += weight * coarse( c, r, 1 );
				vz += weight * coarse( c, r, 2 );
				weights += weight;
			}
			return weights > 1e-6f ? std::hypot( vx, vz ) / weights : 0.0f;
		};
		for( uint32_t t = 0; t < m_detailTileCount; ++t )
			for( uint32_t j = 1; j <= cells; ++j )
			{
				const float* cell = reinterpret_cast<const float*>( infoBytes.data() + infoCopies[t].offset + j * infoCopies[t].rowPitch );
				const float* water = reinterpret_cast<const float*>( waterBytes.data() + waterCopies[t].offset + j * waterCopies[t].rowPitch );
				for( uint32_t i = 1; i <= cells; ++i )
				{
					const float kind = cell[i * 4 + 2];
					const float speed = std::hypot( water[i * 4 + 1], water[i * 4 + 2] );
					if( kind == 2.0f )
					{
						// По глубине: средняя по расходу (у берега мелко и медленно — средняя по ячейкам ниже)
						const float depth = cell[i * 4 + 3];
						const float x = ( m_detailCoords[t].x * cells + i - 0.5f ) * step;
						const float z = ( m_detailCoords[t].y * cells + j - 0.5f ) * step;
						streamSum += depth * speed;
						coarseSum += depth * coarseSpeed( x, z );
						baseSum += depth * std::hypot( cell[i * 4], cell[i * 4 + 1] );
						streamDepth += depth;
						++streamCells;
					}
					else if( kind == 1.0f )
					{
						lakeSum += speed;
						++lakeCells;
					}
					if( kind == 1.0f || kind == 2.0f )
						peak = std::max( peak, speed );
				}
			}
	}
	char text[400];
	snprintf( text, sizeof( text ),
			  "Water detail flow: %u steps x %u iterations over %zu blocks 8x8 (%.2f m); stream cells %u, mean speed by depth %.3f m/s "
			  "(1 m grid %.3f, Manning %.3f); lake cells %u, mean speed %.3f m/s; max %.3f m/s; ms: %.1f (solve %.1f)",
			  steps, iterations, static_cast<size_t>( blocks ), step, streamCells, streamDepth > 0.0 ? streamSum / streamDepth : 0.0,
			  streamDepth > 0.0 ? coarseSum / streamDepth : 0.0, streamDepth > 0.0 ? baseSum / streamDepth : 0.0, lakeCells,
			  lakeCells ? lakeSum / lakeCells : 0.0, peak,
			  std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count(), solveMs );
	LOG( text );
	return text;
}

std::string StaticWater::flowCachePath( const Flow& flow ) const
{
	if( m_flowInputKey == 0 )
		return {};
	Fingerprint key;
	key.add( flowCacheVersion );
	key.add( m_flowInputKey );
	forEachSettingValue( waterFlowSchema(), flow, [&key]( const auto& value ) { key.add( value ); } );
	const TerrainHeight height = m_field->terrain->terrainHeight();
	key.add( height.heightMultiplier );
	key.add( height.heightOffset );
	// Код расчёта — шейдер течения и его include: правка любого — расчёт заново
	for( const char* file : { "Shaders\\water_flow.cs", "Shaders\\water_simulation.sh", "Shaders\\water_flow_constants.h",
							  "Shaders\\water_detail_flow_constants.h", "Shaders\\bindless.sh", "Shaders\\slots.h" } )
	{
		std::ifstream in( file, std::ios::binary );
		const std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
		key.add( text.data(), text.size() );
	}
	char name[64];
	snprintf( name, sizeof( name ), "\\flow_%016llx.bin", static_cast<unsigned long long>( key.value ) );
	return std::string( flowCacheDirectory ) + name;
}

bool StaticWater::loadFlowCache( const std::string& path, const std::function<void()>& buildSurface )
{
	std::ifstream in( path, std::ios::binary );
	FlowCacheHeader header;
	if( !in.read( reinterpret_cast<char*>( &header ), sizeof( header ) ) || std::memcmp( header.magic, "DMWF", 4 ) != 0 ||
		header.version != flowCacheVersion || header.size != m_field->size ||
		( header.detailSlices != 0 && ( header.detailSlices != m_detailTileCount || header.detailSide != m_detailSide ) ) )
		return false;
	std::vector<uint16_t> coarse( static_cast<size_t>( m_field->size ) * m_field->size * 2 );
	std::vector<float> detail( static_cast<size_t>( header.detailSide ) * header.detailSide * header.detailSlices * 2 );
	std::string coarseSummary( header.coarseSummary, ' ' ), detailSummary( header.detailSummary, ' ' );
	if( !in.read( reinterpret_cast<char*>( coarse.data() ), coarse.size() * sizeof( uint16_t ) ) ||
		!in.read( reinterpret_cast<char*>( detail.data() ), detail.size() * sizeof( float ) ) ||
		!in.read( coarseSummary.data(), coarseSummary.size() ) || !in.read( detailSummary.data(), detailSummary.size() ) )
		return false;

	DMD3D& d3d = DMD3D::instance();
	Texture coarseTexture, detailTexture;
	ShaderView coarseView, detailView;
	TextureDesc desc;
	desc.width = m_field->size;
	desc.height = m_field->size;
	desc.format = DXGI_FORMAT_R16G16_FLOAT;
	TextureData coarseData;
	coarseData.data = coarse.data();
	coarseData.rowPitch = m_field->size * 2 * static_cast<uint32_t>( sizeof( uint16_t ) );
	coarseData.slicePitch = coarseData.rowPitch * m_field->size;
	if( !d3d.createTexture( desc, &coarseData, coarseTexture ) || !d3d.createShaderView( coarseTexture, {}, coarseView ) )
		return false;
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	if( header.detailSlices != 0 )
	{
		std::vector<TextureData> slices( header.detailSlices );
		const size_t sliceFloats = static_cast<size_t>( header.detailSide ) * header.detailSide * 2;
		for( uint32_t t = 0; t < header.detailSlices; ++t )
		{
			slices[t].data = detail.data() + t * sliceFloats;
			slices[t].rowPitch = header.detailSide * 2 * static_cast<uint32_t>( sizeof( float ) );
			slices[t].slicePitch = slices[t].rowPitch * header.detailSide;
		}
		TextureDesc detailDesc;
		detailDesc.width = header.detailSide;
		detailDesc.height = header.detailSide;
		detailDesc.arraySize = header.detailSlices;
		detailDesc.format = DXGI_FORMAT_R32G32_FLOAT;
		if( !d3d.createTexture( detailDesc, slices.data(), detailTexture ) || !d3d.createShaderView( detailTexture, arrayView, detailView ) )
			return false;
	}

	// Сетка 1 м — поверх mainStatic, затем поверхность (детальная берёт течение 1 м) и детальная вода поверх неё
	PassDesc pass;
	pass.name = "Water flow from cache";
	pass.reads = { { &coarseView, "cached flow" } };
	pass.writes = { { &m_field->stateUAV, "water state" } };
	d3d.beginPass( pass );
	m_field->setParameters( m_initial );
	d3d.setSRV( 11, coarseView );
	d3d.setUAV( 5, m_field->stateUAV );
	const uint32_t groups = ( m_field->size + 7 ) / 8;
	m_flowRestoreShader.dispatchGroups( groups, groups, 1 );
	buildSurface();
	if( header.detailSlices != 0 )
	{
		PassDesc detailPass;
		detailPass.name = "Water detail flow from cache";
		detailPass.reads = { { &detailView, "cached detail flow" } };
		detailPass.writes = { { &m_detailWaterUAV, "detail water" } };
		d3d.beginPass( detailPass );
		d3d.setSRV( 11, detailView );
		d3d.setUAV( 5, m_detailWaterUAV );
		const uint32_t detailGroups = ( header.detailSide + 7 ) / 8;
		m_detailFlowRestoreShader.dispatchGroups( detailGroups, detailGroups, header.detailSlices );
	}
	// Временные текстуры освобождаются при выходе: GPU должен закончить с ними
	d3d.waitForGpu();
	if( !coarseSummary.empty() )
		LOG( "  cached: " + coarseSummary );
	if( !detailSummary.empty() )
		LOG( "  cached: " + detailSummary );
	return true;
}

void StaticWater::saveFlowCache( const std::string& path, const std::string& coarseSummary, const std::string& detailSummary )
{
	DMD3D& d3d = DMD3D::instance();
	std::vector<DMD3D::SubresourceCopy> coarseCopies, detailCopies;
	std::vector<uint8_t> coarseBytes, detailBytes;
	if( !d3d.captureTexture( m_field->state, coarseCopies, coarseBytes ) || coarseCopies.empty() )
	{
		LOG( "Water flow: cache not written, readback failed" );
		return;
	}
	FlowCacheHeader header;
	header.size = m_field->size;
	header.coarseSummary = static_cast<uint32_t>( coarseSummary.size() );
	header.detailSummary = static_cast<uint32_t>( detailSummary.size() );
	// m_field->state — R16G16B16A16_FLOAT: течение — каналы 1, 2
	std::vector<uint16_t> coarse( static_cast<size_t>( m_field->size ) * m_field->size * 2 );
	for( uint32_t row = 0; row < m_field->size; ++row )
	{
		const uint16_t* line = reinterpret_cast<const uint16_t*>( coarseBytes.data() + coarseCopies[0].offset + row * coarseCopies[0].rowPitch );
		for( uint32_t col = 0; col < m_field->size; ++col )
		{
			coarse[( static_cast<size_t>( row ) * m_field->size + col ) * 2] = line[col * 4 + 1];
			coarse[( static_cast<size_t>( row ) * m_field->size + col ) * 2 + 1] = line[col * 4 + 2];
		}
	}
	std::vector<float> detail;
	if( !detailSummary.empty() && d3d.captureTexture( m_detailWater, detailCopies, detailBytes ) && detailCopies.size() >= m_detailTileCount )
	{
		header.detailSide = m_detailSide;
		header.detailSlices = m_detailTileCount;
		detail.resize( static_cast<size_t>( m_detailSide ) * m_detailSide * m_detailTileCount * 2 );
		for( uint32_t t = 0; t < m_detailTileCount; ++t )
			for( uint32_t j = 0; j < m_detailSide; ++j )
			{
				const float* line = reinterpret_cast<const float*>( detailBytes.data() + detailCopies[t].offset + j * detailCopies[t].rowPitch );
				float* out = detail.data() + ( ( static_cast<size_t>( t ) * m_detailSide + j ) * m_detailSide ) * 2;
				for( uint32_t i = 0; i < m_detailSide; ++i )
				{
					out[i * 2] = line[i * 4 + 1];
					out[i * 2 + 1] = line[i * 4 + 2];
				}
			}
	}
	std::error_code error;
	std::filesystem::create_directories( flowCacheDirectory, error );
	{
		std::ofstream out( path, std::ios::binary | std::ios::trunc );
		out.write( reinterpret_cast<const char*>( &header ), sizeof( header ) );
		out.write( reinterpret_cast<const char*>( coarse.data() ), coarse.size() * sizeof( uint16_t ) );
		out.write( reinterpret_cast<const char*>( detail.data() ), detail.size() * sizeof( float ) );
		out.write( coarseSummary.data(), coarseSummary.size() );
		out.write( detailSummary.data(), detailSummary.size() );
		if( !out )
		{
			LOG( "Water flow: can`t write cache " + path );
			return;
		}
	}
	// Хранятся последние flowCacheFiles: русла правят вживую, и каждый вариант — свой файл
	std::vector<std::filesystem::directory_entry> files;
	for( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( flowCacheDirectory, error ) )
		if( entry.is_regular_file() && entry.path().filename().string().rfind( "flow_", 0 ) == 0 )
			files.push_back( entry );
	std::sort( files.begin(), files.end(), []( const auto& a, const auto& b ) { return a.last_write_time() > b.last_write_time(); } );
	for( size_t i = flowCacheFiles; i < files.size(); ++i )
		std::filesystem::remove( files[i].path(), error );
	LOG( "Water flow: cache written " + path );
}

void StaticWater::solve( const Flow& flow, const std::function<void()>& buildSurface )
{
	const auto start = std::chrono::high_resolution_clock::now();
	const std::string path = flowCachePath( flow );
	SlowTask task( 1.0f );
	if( !path.empty() )
	{
		task.enterProgressFrame( 0.0f, "Cache" );
		if( loadFlowCache( path, buildSurface ) )
		{
			m_flowBuilt = flow;
			LOG( "Water flow: from cache " + path + ", ms: " +
				 std::to_string( std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count() ) );
			return;
		}
	}
	task.enterProgressFrame( 0.5f, "1 m grid" );
	const std::string coarse = solveFlow( flow );
	buildSurface();
	task.enterProgressFrame( 0.5f, "Detail grid" );
	const std::string detail = solveDetailFlow( flow );
	// Под GPU-based validation расчёт урезан — такой в кэш не пишется
	if( !path.empty() && !coarse.empty() && !DMD3D::instance().gpuValidation() )
		saveFlowCache( path, coarse, detail );
}

void StaticWater::buildDetailSurface()
{
	if( m_detailTileCount == 0 )
		return;
	DMD3D& d3d = DMD3D::instance();
	const TerrainHeight height = m_field->terrain->terrainHeight();
	if( !height.detailTiles )
		return;
	PassDesc pass;
	pass.name = "Water detail surface";
	pass.reads = { { &m_detailStaticView, "detail static water" }, { height.detailTiles, "terrain detail tiles" },
				   { &m_field->stateView, "water state" }, { &m_detailTileCoordsView, "detail tile coordinates" } };
	pass.writes = { { &m_detailWaterUAV, "detail water" }, { &m_detailBoundsUAV, "detail tile bounds" } };
	d3d.beginPass( pass );
	WaterDetailBuffer params = {};
	params.heightMultiplier = height.heightMultiplier;
	params.heightOffset = height.heightOffset;
	params.worldSize = height.worldSize;
	params.tileSize = m_detailTileSize;
	params.visibleDepth = visibleDepth;
	params.side = m_detailSide;
	Device::updateResourceData( m_detailBuffer, params );
	d3d.setConstantBuffer( 5, m_detailBuffer );
	// Границы уровней плиток — наибольшие значения (атомарный max с нуля): нули — плитка без воды
	d3d.clearStorageView( m_detailBoundsUAV );
	d3d.setSRV( 0, m_detailStaticView );
	d3d.setSRV( 1, *height.detailTiles );
	d3d.setSRV( 2, m_field->stateView );
	d3d.setSRV( 3, m_detailTileCoordsView );
	d3d.setUAV( 0, m_detailWaterUAV );
	d3d.setUAV( 1, m_detailBoundsUAV );
	const uint32_t groups = ( m_detailSide + 7 ) / 8;
	m_detailSurfaceShader.dispatchGroups( groups, groups, m_detailTileCount );
}

bool StaticWater::exportDetail( const std::string& prefix, std::string& reason )
{
	// Детальная вода: плитки — в растр мира (внутренние тексели, без поля), строка 0 — дальний край по z. Итог расчёта
	// (уровень, течение, глубина) и его вход из конвейера (течение по Маннингу u₀ и гидравлический радиус R русла)
	if( m_detailTileCount == 0 )
		return true;
	const uint32_t inner = m_detailSide - 2;
	const uint32_t side = static_cast<uint32_t>( std::lround( m_field->terrain->terrainHeight().worldSize / m_detailTileSize ) ) * inner;
	const auto exportTexture = [&]( const Texture& texture, const float fill[4], const char* const names[4], std::string& error )
	{
		std::vector<DMD3D::SubresourceCopy> copies;
		std::vector<uint8_t> bytes;
		if( !DMD3D::instance().captureTexture( texture, copies, bytes ) || copies.size() < m_detailTileCount )
		{
			error = "water detail readback failed";
			return false;
		}
		std::vector<float> detail[4];
		for( int c = 0; c < 4; ++c )
			detail[c].assign( static_cast<size_t>( side ) * side, fill[c] );
		for( uint32_t t = 0; t < m_detailTileCount; ++t )
		{
			for( uint32_t j = 1; j <= inner; ++j )
			{
				const float* texel = reinterpret_cast<const float*>( bytes.data() + copies[t].offset + j * copies[t].rowPitch );
				const uint32_t z = m_detailCoords[t].y * inner + j - 1;
				if( z >= side )
					continue;
				for( uint32_t i = 1; i <= inner; ++i )
				{
					const uint32_t x = m_detailCoords[t].x * inner + i - 1;
					if( x >= side )
						continue;
					const size_t index = static_cast<size_t>( side - 1 - z ) * side + x;
					for( int c = 0; c < 4; ++c )
						detail[c][index] = texel[i * 4 + c];
				}
			}
		}
		for( int c = 0; c < 4; ++c )
		{
			if( !names[c] )
				continue;
			const std::string file = prefix + names[c];
			if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), side, side, detail[c].data() ) )
			{
				error = "cannot write " + file;
				return false;
			}
		}
		return true;
	};
	const float fill[4] = { -1e9f, 0.0f, 0.0f, 0.0f };
	const char* const stateNames[4] = { "_detail_level.dds", "_detail_vx.dds", "_detail_vz.dds", "_detail_depth.dds" };
	const char* const inputNames[4] = { nullptr, "_detail_u0x.dds", "_detail_u0z.dds", "_detail_radius.dds" };
	if( !exportTexture( m_detailWater, fill, stateNames, reason ) || !exportTexture( m_detailStatic, fill, inputNames, reason ) )
		return false;
	LOG( "Water detail state: " + prefix + "_detail_level.dds, _detail_vx.dds, _detail_vz.dds, _detail_depth.dds, _detail_u0x.dds, "
		 "_detail_u0z.dds, _detail_radius.dds" );
	return true;
}

}
