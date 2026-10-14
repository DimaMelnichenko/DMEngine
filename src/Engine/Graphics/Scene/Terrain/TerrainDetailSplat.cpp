#include "TerrainDetailSplat.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <execution>
#include <numeric>
#include <string>
#include "TerrainEdits.h"
#include "TerrainMasks.h"
#include "TerrainPaint.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

constexpr uint32_t layers = TerrainMasks::splatLayers;
constexpr uint32_t weightSlices = layers / 4;
constexpr uint32_t slicesPerTile = weightSlices + 1;	// и срез не весов (мокрость)
constexpr uint32_t spareTiles = 16;						// мест в запасе: новая плитка мазка — без пересоздания массива
constexpr float noLevel = -1e30f;

float smoothstep( float from, float to, float x )
{
	const float t = std::clamp( ( x - from ) / ( to - from ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

// Доля диапазона правила: 0 при from, 1 при to (в любую сторону), плавно; from = to — ступень
float ramp( const TerrainLayerRules::Range& range, float x )
{
	if( std::fabs( range.to - range.from ) < 1e-6f )
		return x >= range.from ? 1.0f : 0.0f;
	return smoothstep( range.from, range.to, x );
}

uint8_t store( float value )
{
	return static_cast<uint8_t>( std::lround( std::clamp( value, 0.0f, 1.0f ) * 255.0f ) );
}

// Билинейно по центрам клеток сетки size² (строка 0 — дальний край по z) в точке мира (x, z)
float sampleCells( const float* values, int size, float texel, float x, float z )
{
	const float u = x / texel - 0.5f;
	const float v = ( size * texel - z ) / texel - 0.5f;
	const int c0 = std::clamp( static_cast<int>( std::floor( u ) ), 0, size - 1 );
	const int r0 = std::clamp( static_cast<int>( std::floor( v ) ), 0, size - 1 );
	const int c1 = std::min( c0 + 1, size - 1 );
	const int r1 = std::min( r0 + 1, size - 1 );
	const float fu = std::clamp( u - std::floor( u ), 0.0f, 1.0f );
	const float fv = std::clamp( v - std::floor( v ), 0.0f, 1.0f );
	const auto at = [&]( int c, int r ) { return values[static_cast<size_t>( r ) * size + c]; };
	const float upper = at( c0, r0 ) + ( at( c1, r0 ) - at( c0, r0 ) ) * fu;
	const float lower = at( c0, r1 ) + ( at( c1, r1 ) - at( c0, r1 ) ) * fu;
	return upper + ( lower - upper ) * fv;
}

// Уровень воды сетки 1 м в точке, как waterDepthAt: билинейно по четырём клеткам вокруг, клетка без уровня — наименьший
// из соседних; воды рядом нет — noLevel
float cellWaterLevel( const std::vector<float>& level, int size, float texel, float x, float z )
{
	const float u = x / texel - 0.5f;
	const float v = ( size * texel - z ) / texel - 0.5f;
	const int c = static_cast<int>( std::floor( u ) );
	const int r = static_cast<int>( std::floor( v ) );
	const float fu = u - c;
	const float fv = v - r;
	float levels[4];
	float lowest = 1e30f;
	for( int k = 0; k < 4; ++k )
	{
		const int cc = std::clamp( c + ( k & 1 ), 0, size - 1 );
		const int rr = std::clamp( r + ( k >> 1 ), 0, size - 1 );
		levels[k] = level[static_cast<size_t>( rr ) * size + cc];
		if( levels[k] > -1e29f )
			lowest = std::min( lowest, levels[k] );
	}
	if( lowest > 1e29f )
		return noLevel;
	for( float& value : levels )
		value = value > -1e29f ? value : lowest;
	const float upper = levels[0] + ( levels[1] - levels[0] ) * fu;
	const float lower = levels[2] + ( levels[3] - levels[2] ) * fu;
	return upper + ( lower - upper ) * fv;
}

}

void TerrainDetailSplat::computeSource( Tile& tile, const Source& source ) const
{
	const int side = static_cast<int>( m_side );
	const size_t texels = static_cast<size_t>( side ) * side;
	const TerrainMasks& masks = *source.masks;
	const int size = static_cast<int>( masks.size );
	const float worldSize = masks.size * source.texelSize;
	const float cell = m_tileSize / ( side - 2 );
	const TerrainHydrology::DetailTile* hydrology =
		tile.hydrology >= 0 && source.hydrology && static_cast<size_t>( tile.hydrology ) < source.hydrology->size() ?
		&( *source.hydrology )[tile.hydrology] : nullptr;
	const bool channels = hydrology && source.channelLayer >= 0 && hydrology->carve.size() == texels;
	tile.source.assign( slicesPerTile * texels * 4, 0 );

	// Высота точной земли в центре текселя: детальная плитка или карта высот
	const auto texelCenter = [&]( int i, int j, float& x, float& z )
	{
		x = tile.x * m_tileSize + ( i - 0.5f ) * cell;
		z = tile.z * m_tileSize + ( j - 0.5f ) * cell;
	};
	std::vector<float> ground;
	if( source.staticWater )
	{
		ground.resize( texels );
		for( int j = 0; j < side; ++j )
			for( int i = 0; i < side; ++i )
			{
				const size_t k = static_cast<size_t>( j ) * side + i;
				float x, z;
				texelCenter( i, j, x, z );
				if( hydrology && hydrology->heights.size() == texels )
					ground[k] = hydrology->heights[k];
				else if( source.heights )
					ground[k] = sampleCells( source.heights, size, source.texelSize, x, z ) * source.heightMultiplier + source.heightOffset;
				else
					ground[k] = 1e30f;
			}
	}

	// Уровень воды в текселях: у русел — как у детальной воды (water_detail.cs, mainDetailSurface: свой, если вода видна,
	// иначе наименьший у мокрых соседей), затем как waterDepthAt в центре текселя (нет уровня — наименьший из 2 × 2); у
	// плиток мазков — уровень сетки 1 м в точке, как waterDepthAt вне детальной воды
	std::vector<float> level;
	if( source.staticWater && hydrology && hydrology->water.size() == texels )
	{
		std::vector<float> surface( texels, noLevel );
		const auto depth = [&]( int i, int j, float& waterLevel )
		{
			const size_t k = static_cast<size_t>( j ) * side + i;
			waterLevel = hydrology->water[k].x;
			return waterLevel > -1e8f ? waterLevel - ground[k] : noLevel;
		};
		for( int j = 0; j < side; ++j )
			for( int i = 0; i < side; ++i )
			{
				float own;
				if( depth( i, j, own ) >= TerrainHydrology::visibleWaterDepth )
				{
					surface[static_cast<size_t>( j ) * side + i] = own;
					continue;
				}
				float lowest = 1e30f;
				for( int y = -1; y <= 1; ++y )
					for( int x = -1; x <= 1; ++x )
					{
						float neighbor;
						if( depth( std::clamp( i + x, 0, side - 1 ), std::clamp( j + y, 0, side - 1 ), neighbor ) >= TerrainHydrology::visibleWaterDepth )
							lowest = std::min( lowest, neighbor );
					}
				if( lowest < 1e29f )
					surface[static_cast<size_t>( j ) * side + i] = lowest;
			}
		level.assign( texels, noLevel );
		for( int j = 0; j < side; ++j )
			for( int i = 0; i < side; ++i )
			{
				const size_t k = static_cast<size_t>( j ) * side + i;
				float value = surface[k];
				if( value < -1e29f )
				{
					float lowest = 1e30f;
					for( int n = 1; n < 4; ++n )
					{
						const float neighbor = surface[static_cast<size_t>( std::min( j + ( n >> 1 ), side - 1 ) ) * side + std::min( i + ( n & 1 ), side - 1 )];
						if( neighbor > -1e29f )
							lowest = std::min( lowest, neighbor );
					}
					value = lowest < 1e29f ? lowest : noLevel;
				}
				level[k] = value;
			}
	}
	else if( source.staticWater && !hydrology && source.waterLevel && source.waterLevel->size() == static_cast<size_t>( size ) * size )
	{
		level.assign( texels, noLevel );
		for( int j = 0; j < side; ++j )
			for( int i = 0; i < side; ++i )
			{
				float x, z;
				texelCenter( i, j, x, z );
				level[static_cast<size_t>( j ) * side + i] = cellWaterLevel( *source.waterLevel, size, source.texelSize, x, z );
			}
	}

	for( int j = 0; j < side; ++j )
	{
		for( int i = 0; i < side; ++i )
		{
			const size_t k = static_cast<size_t>( j ) * side + i;
			float x, z;
			texelCenter( i, j, x, z );
			// Основа — правила сетки 1 м билинейно по центрам клеток (как Sample splat-карты)
			float weights[layers];
			for( uint32_t layer = 0; layer < layers; ++layer )
				weights[layer] = masks.splat[layer].empty() ? 0.0f : sampleCells( masks.splat[layer].data(), size, source.texelSize, x, z );
			// Покраска ручными правками — поверх (как в splat-карте: вес × remaining + покраска слоя)
			const TerrainEditCoverage* edits = source.edits;
			if( edits && edits->paints() )
			{
				const float eu = x / worldSize;
				const float ev = ( worldSize - z ) / worldSize;
				const float remaining = edits->sample( edits->remaining, eu, ev );
				if( remaining < 1.0f )
					for( uint32_t layer = 0; layer < layers; ++layer )
					{
						const std::vector<float>& painted = edits->paint[layer];
						weights[layer] = weights[layer] * remaining +
										 ( layer < source.layerCount && !painted.empty() ? edits->sample( painted, eu, ev ) : 0.0f );
					}
			}
			// Дно на долю share: галька русел, из неё — доли дресвы и ила (наносы)
			const auto paintBed = [&]( float share, float gravel, float silt )
			{
				const float pebbles = 1.0f - gravel - silt;
				for( uint32_t layer = 0; layer < layers; ++layer )
				{
					const int32_t index = static_cast<int32_t>( layer );
					const float bed = ( index == source.channelLayer ? pebbles : 0.0f ) + ( index == source.gravelLayer ? gravel : 0.0f ) +
									  ( index == source.siltLayer ? silt : 0.0f );
					weights[layer] = weights[layer] * ( 1.0f - share ) + share * bed;
				}
			};
			const auto paint = [&]( float share ) { paintBed( share, 0.0f, 0.0f ); };
			// Галька русел — по врезу в текселе
			if( channels )
				paint( smoothstep( 0.03f, 0.15f, hydrology->carve[k] ) );
			// Дно под водой и мокрая земля — по глубине над точной землёй
			float wetness = 0.0f;
			if( !level.empty() )
			{
				const float waterDepth = level[k] > -1e29f && ground[k] < 1e29f ? level[k] - ground[k] : noLevel;
				if( source.channelLayer >= 0 && waterDepth > 0.03f )
				{
					// Наносы (как lakeBedShares в terrain.ps): у озера — по глубине, если озеро нижнее; у ручья — дресва на дне
					// медленного участка. Озеро или ручей — по детальной воде текселя: у озера течения и R нет; уровень от
					// соседей (урез) — галька. Плитка мазка без русел — озеро
					float gravel = 0.0f, silt = 0.0f;
					if( source.bedRules && source.gravelLayer >= 0 )
					{
						const TerrainLayerRules& rules = *source.bedRules;
						const DirectX::XMFLOAT4 own = hydrology && hydrology->water.size() == texels ? hydrology->water[k] : DirectX::XMFLOAT4( level[k], 0.0f, 0.0f, 0.0f );
						if( own.x > -1e8f && own.w <= 0.0f )
						{
							const float height = ( level[k] - source.heightOffset ) / std::max( source.heightMultiplier, 1e-6f );
							const float sediment = ramp( rules.lakeSedimentHeight, height );
							gravel = sediment * ramp( rules.lakeGravelDepth, waterDepth );
							silt = std::min( sediment * ramp( rules.lakeSiltDepth, waterDepth ), gravel );
							if( source.siltLayer < 0 )
								silt = 0.0f;
							gravel -= silt;
						}
						else if( own.x > -1e8f )
							gravel = ramp( rules.streamGravelSpeed, std::hypot( own.y, own.z ) ) * ramp( rules.streamGravelDepth, waterDepth );
					}
					paintBed( smoothstep( 0.03f, 0.15f, waterDepth ), gravel, silt );
				}
				wetness = smoothstep( -0.02f, 0.0f, waterDepth );
			}
			for( uint32_t slice = 0; slice < weightSlices; ++slice )
			{
				uint8_t* texel = &tile.source[( slice * texels + k ) * 4];
				for( uint32_t c = 0; c < 4; ++c )
					texel[c] = store( weights[slice * 4 + c] );
			}
			tile.source[( weightSlices * texels + k ) * 4] = store( wetness );
		}
	}
}

void TerrainDetailSplat::computeFinal( const Tile& tile, const Source& source, std::vector<uint8_t>& out ) const
{
	out = tile.source;
	if( !source.paint )
		return;
	// Мазки тонкой сетки в прямоугольнике плитки с полем: тексель (i, j) — столбец x · 128 + i − 1, строка (от дальнего края)
	// fine − z · 128 − j; блок поправок — строки по возрастанию, b = side − 1 − j
	const int32_t side = static_cast<int32_t>( m_side );
	const int32_t inner = side - 2;
	const int32_t x0 = static_cast<int32_t>( tile.x ) * inner - 1;
	const int32_t row0 = static_cast<int32_t>( m_fineSize ) - static_cast<int32_t>( tile.z ) * inner - ( side - 1 );
	std::vector<std::pair<int32_t, std::vector<float>>> painted;	// слой (−1 — мокрость) и поправки
	for( const auto& [target, count] : source.paint->targets() )
	{
		if( !TerrainPaint::isFine( target ) )
			continue;
		const int32_t layer = TerrainPaint::layerOf( target );
		if( layer >= static_cast<int32_t>( source.layerCount ) )
			continue;
		std::vector<float> block;
		if( source.paint->deltas( target, x0, row0, m_side, m_side, block ) )
			painted.emplace_back( layer, std::move( block ) );
	}
	if( painted.empty() )
		return;
	std::sort( painted.begin(), painted.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );
	const size_t texels = static_cast<size_t>( side ) * side;
	for( int32_t j = 0; j < side; ++j )
		for( int32_t i = 0; i < side; ++i )
		{
			const size_t k = static_cast<size_t>( j ) * side + i;
			const size_t b = static_cast<size_t>( side - 1 - j ) * side + i;
			float weights[layers];
			for( uint32_t layer = 0; layer < layers; ++layer )
				weights[layer] = out[( ( layer / 4 ) * texels + k ) * 4 + layer % 4] / 255.0f;
			float wetness = out[( weightSlices * texels + k ) * 4] / 255.0f;
			bool changed = false;
			for( const auto& [layer, block] : painted )
			{
				const float d = block[b];
				if( d == 0.0f )
					continue;
				changed = true;
				if( layer < 0 )
					wetness = TerrainPaint::result( false, wetness, d );
				else
					TerrainPaint::applyLayerDelta( weights, source.layerCount, static_cast<uint32_t>( layer ), d );
			}
			if( !changed )
				continue;
			for( uint32_t layer = 0; layer < layers; ++layer )
				out[( ( layer / 4 ) * texels + k ) * 4 + layer % 4] = store( weights[layer] );
			out[( weightSlices * texels + k ) * 4] = store( wetness );
		}
}

bool TerrainDetailSplat::createArray( const Source& source )
{
	// Все места: итог плиток (параллельно), запасные — нули; без плиток — места 1 × 1
	const bool any = !m_tiles.empty();
	const size_t sliceBytes = any ? static_cast<size_t>( m_side ) * m_side * 4 : 4;
	m_capacity = static_cast<uint32_t>( m_tiles.size() ) + spareTiles;
	const size_t slices = static_cast<size_t>( m_capacity ) * slicesPerTile;
	std::vector<uint8_t> data( slices * sliceBytes, 0 );
	if( any )
	{
		std::vector<size_t> order( m_tiles.size() );
		std::iota( order.begin(), order.end(), size_t( 0 ) );
		std::for_each( std::execution::par, order.begin(), order.end(), [&]( size_t t )
		{
			std::vector<uint8_t> final;
			computeFinal( m_tiles[t], source, final );
			std::copy( final.begin(), final.end(), data.begin() + t * slicesPerTile * sliceBytes );
		} );
	}
	TextureDesc desc;
	desc.width = desc.height = any ? m_side : 1;
	desc.arraySize = static_cast<uint32_t>( slices );
	desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	std::vector<TextureData> initial( slices );
	for( size_t s = 0; s < slices; ++s )
	{
		initial[s].data = &data[s * sliceBytes];
		initial[s].rowPitch = desc.width * 4;
	}
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createTexture( desc, initial.data(), m_texture ) || !d3d.createShaderView( m_texture, arrayView, m_view ) )
	{
		LOG( "Terrain detail splat: the tile array is not created" );
		return false;
	}
	d3d.setName( m_texture, "Terrain detail splat" );
	return true;
}

bool TerrainDetailSplat::uploadIndex()
{
	// Индекс: плитка мира → место + 1 (0 — нет); без сетки — 1 × 1 ноль. Есть того же размера — участком, иначе заново
	const uint32_t side = std::max( m_tilesPerSide, 1u );
	std::vector<uint16_t> index( static_cast<size_t>( side ) * side, 0 );
	for( size_t i = 0; i < m_slot.size() && i < index.size(); ++i )
		index[i] = static_cast<uint16_t>( m_slot[i] + 1 );
	DMD3D& d3d = DMD3D::instance();
	if( m_indexTexture.valid() && m_indexTexture.width() == side )
		return d3d.updateTexture( m_indexTexture, TextureRegion(), index.data(), side * sizeof( uint16_t ) );
	TextureDesc desc;
	desc.width = desc.height = side;
	desc.format = DXGI_FORMAT_R16_UINT;
	TextureData data;
	data.data = index.data();
	data.rowPitch = side * sizeof( uint16_t );
	if( !d3d.createTexture( desc, &data, m_indexTexture ) || !d3d.createShaderView( m_indexTexture, {}, m_indexView ) )
	{
		LOG( "Terrain detail splat: the tile index is not created" );
		return false;
	}
	d3d.setName( m_indexTexture, "Terrain detail splat index" );
	return true;
}

void TerrainDetailSplat::upload( size_t slot, const std::vector<uint8_t>& data )
{
	const size_t sliceBytes = static_cast<size_t>( m_side ) * m_side * 4;
	for( uint32_t slice = 0; slice < slicesPerTile; ++slice )
	{
		TextureRegion region;
		region.slice = static_cast<uint32_t>( slot ) * slicesPerTile + slice;
		DMD3D::instance().updateTexture( m_texture, region, &data[slice * sliceBytes], m_side * 4 );
	}
}

bool TerrainDetailSplat::build( const Source& source, uint32_t tilesPerSide, const std::vector<TileKey>& extra )
{
	const auto start = std::chrono::steady_clock::now();
	m_side = TerrainHydrology::detailTileCells * TerrainHydrology::detailSamples + 2;
	const bool usable = source.masks && source.masks->size > 0 && tilesPerSide > 0;
	m_tileSize = usable ? TerrainHydrology::detailTileCells * source.texelSize : 0.0f;
	m_tilesPerSide = usable ? tilesPerSide : 0;
	m_fineSize = usable ? source.masks->size * TerrainPaint::fineScale : 0;
	m_tiles.clear();
	m_slot.assign( static_cast<size_t>( m_tilesPerSide ) * m_tilesPerSide, -1 );
	if( usable )
	{
		const auto add = [&]( uint32_t x, uint32_t z, int32_t hydrology )
		{
			if( x >= m_tilesPerSide || z >= m_tilesPerSide )
				return;
			int32_t& slot = m_slot[static_cast<size_t>( z ) * m_tilesPerSide + x];
			if( slot >= 0 )
				return;
			slot = static_cast<int32_t>( m_tiles.size() );
			Tile& tile = m_tiles.emplace_back();
			tile.x = x;
			tile.z = z;
			tile.hydrology = hydrology;
		};
		if( source.hydrology )
			for( size_t t = 0; t < source.hydrology->size(); ++t )
				add( ( *source.hydrology )[t].x, ( *source.hydrology )[t].z, static_cast<int32_t>( t ) );
		for( const TileKey& key : extra )
			add( key.first, key.second, -1 );
		std::for_each( std::execution::par, m_tiles.begin(), m_tiles.end(), [&]( Tile& tile ) { computeSource( tile, source ); } );
	}
	if( !createArray( source ) || !uploadIndex() )
		return false;
	if( !m_tiles.empty() )
		LOG( "Terrain detail splat: tiles " + std::to_string( m_tiles.size() ) + " (" +
			 std::to_string( std::count_if( m_tiles.begin(), m_tiles.end(), []( const Tile& tile ) { return tile.hydrology < 0; } ) ) +
			 " of brush strokes, " +
			 std::to_string( static_cast<size_t>( m_capacity ) * slicesPerTile * m_side * m_side * 4 / 1024 / 1024 ) + " MB), ms: " +
			 std::to_string( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() ) );
	return true;
}

void TerrainDetailSplat::ensure( const Source& source, const std::vector<TileKey>& tiles )
{
	if( m_tileSize <= 0.0f )
		return;
	const size_t first = m_tiles.size();
	for( const TileKey& key : tiles )
	{
		if( key.first >= m_tilesPerSide || key.second >= m_tilesPerSide )
			continue;
		int32_t& slot = m_slot[static_cast<size_t>( key.second ) * m_tilesPerSide + key.first];
		if( slot >= 0 )
			continue;
		slot = static_cast<int32_t>( m_tiles.size() );
		Tile& tile = m_tiles.emplace_back();
		tile.x = key.first;
		tile.z = key.second;
	}
	if( m_tiles.size() == first )
		return;
	std::for_each( std::execution::par, m_tiles.begin() + first, m_tiles.end(), [&]( Tile& tile ) { computeSource( tile, source ); } );
	// Мест не хватило — массив заново (с новым запасом), иначе новые места участками
	if( m_tiles.size() > m_capacity )
		createArray( source );
	else
		for( size_t t = first; t < m_tiles.size(); ++t )
		{
			std::vector<uint8_t> final;
			computeFinal( m_tiles[t], source, final );
			upload( t, final );
		}
	uploadIndex();
}

void TerrainDetailSplat::refresh( const Source& source, const std::vector<TileKey>& tiles )
{
	std::vector<size_t> slots;
	for( const TileKey& key : tiles )
		if( key.first < m_tilesPerSide && key.second < m_tilesPerSide )
			if( const int32_t slot = m_slot[static_cast<size_t>( key.second ) * m_tilesPerSide + key.first]; slot >= 0 )
				slots.push_back( static_cast<size_t>( slot ) );
	if( slots.empty() )
		return;
	std::vector<std::vector<uint8_t>> finals( slots.size() );
	std::vector<size_t> order( slots.size() );
	std::iota( order.begin(), order.end(), size_t( 0 ) );
	std::for_each( std::execution::par, order.begin(), order.end(), [&]( size_t n ) { computeFinal( m_tiles[slots[n]], source, finals[n] ); } );
	for( size_t n = 0; n < slots.size(); ++n )
		upload( slots[n], finals[n] );
}

std::vector<TerrainDetailSplat::TileKey> TerrainDetailSplat::tilesOf( uint32_t x0, uint32_t row0, uint32_t x1, uint32_t row1, bool border,
																	  bool existing ) const
{
	std::vector<TileKey> result;
	if( m_tileSize <= 0.0f || x0 >= x1 || row0 >= row1 )
		return result;
	// Строки тонкой сетки — от дальнего края, плитки — от ближнего: f = fine − 1 − row. Тексель края — и поле соседней плитки
	const int32_t inner = static_cast<int32_t>( m_side ) - 2;
	const int32_t fine = static_cast<int32_t>( m_fineSize );
	const int32_t f0 = fine - static_cast<int32_t>( row1 );
	const int32_t f1 = fine - 1 - static_cast<int32_t>( row0 );
	const int32_t pad = border ? 1 : 0;
	const auto floorDiv = [inner]( int32_t value ) { return value >= 0 ? value / inner : -( ( -value + inner - 1 ) / inner ); };
	const int32_t last = static_cast<int32_t>( m_tilesPerSide ) - 1;
	const int32_t tx0 = std::max( floorDiv( static_cast<int32_t>( x0 ) - pad ), 0 );
	const int32_t tx1 = std::min( floorDiv( static_cast<int32_t>( x1 ) - 1 + pad ), last );
	const int32_t tz0 = std::max( floorDiv( f0 - pad ), 0 );
	const int32_t tz1 = std::min( floorDiv( f1 + pad ), last );
	for( int32_t tz = tz0; tz <= tz1; ++tz )
		for( int32_t tx = tx0; tx <= tx1; ++tx )
			if( !existing || m_slot[static_cast<size_t>( tz ) * m_tilesPerSide + tx] >= 0 )
				result.emplace_back( static_cast<uint32_t>( tx ), static_cast<uint32_t>( tz ) );
	return result;
}

bool TerrainDetailSplat::sourceAt( uint32_t x, uint32_t row, uint32_t channel, float& value ) const
{
	if( m_tileSize <= 0.0f || x >= m_fineSize || row >= m_fineSize || channel > wetnessChannel )
		return false;
	const uint32_t inner = m_side - 2;
	const uint32_t f = m_fineSize - 1 - row;
	const uint32_t tx = x / inner;
	const uint32_t tz = f / inner;
	if( tx >= m_tilesPerSide || tz >= m_tilesPerSide )
		return false;
	const int32_t slot = m_slot[static_cast<size_t>( tz ) * m_tilesPerSide + tx];
	if( slot < 0 )
		return false;
	const size_t texels = static_cast<size_t>( m_side ) * m_side;
	const size_t k = static_cast<size_t>( f - tz * inner + 1 ) * m_side + ( x - tx * inner + 1 );
	const uint32_t slice = channel < layers ? channel / 4 : weightSlices;
	const uint32_t component = channel < layers ? channel % 4 : 0;
	value = m_tiles[slot].source[( slice * texels + k ) * 4 + component] / 255.0f;
	return true;
}

void TerrainDetailSplat::bind() const
{
	DMD3D::instance().setSRV( SLOT_TERRAIN_SPLAT_DETAIL, m_view );
	DMD3D::instance().setSRV( SLOT_TERRAIN_SPLAT_DETAIL_INDEX, m_indexView );
}

}
