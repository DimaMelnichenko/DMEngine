#include "TerrainDetailSplat.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <execution>
#include <numeric>
#include <string>
#include "TerrainEdits.h"
#include "TerrainMasks.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

constexpr uint32_t layers = TerrainMasks::splatLayers;
constexpr uint32_t slicesPerTile = layers / 4;

float smoothstep( float from, float to, float x )
{
	const float t = std::clamp( ( x - from ) / ( to - from ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

}

void TerrainDetailSplat::computeTile( const TerrainHydrology::DetailTile& tile, const Source& source, uint8_t* out ) const
{
	const int side = static_cast<int>( m_side );
	const size_t texels = static_cast<size_t>( side ) * side;
	const TerrainMasks& masks = *source.masks;
	const int size = static_cast<int>( masks.size );
	const float worldSize = masks.size * source.texelSize;
	const float cell = m_tileSize / ( side - 2 );
	const bool channels = source.channelLayer >= 0 && tile.carve.size() == texels;

	for( int j = 0; j < side; ++j )
	{
		for( int i = 0; i < side; ++i )
		{
			const size_t k = static_cast<size_t>( j ) * side + i;
			// Основа — сетка 1 м билинейно по центрам клеток (как Sample splat-карты); тексель i плитки — центр в
			// ( i − 1 + 0,5 ) · cell от её края
			const float x = tile.x * m_tileSize + ( i - 0.5f ) * cell;
			const float z = tile.z * m_tileSize + ( j - 0.5f ) * cell;
			const float u = x / source.texelSize - 0.5f;
			const float v = ( worldSize - z ) / source.texelSize - 0.5f;
			const int c0 = std::clamp( static_cast<int>( std::floor( u ) ), 0, size - 1 );
			const int r0 = std::clamp( static_cast<int>( std::floor( v ) ), 0, size - 1 );
			const int c1 = std::min( c0 + 1, size - 1 );
			const int r1 = std::min( r0 + 1, size - 1 );
			const float fu = std::clamp( u - std::floor( u ), 0.0f, 1.0f );
			const float fv = std::clamp( v - std::floor( v ), 0.0f, 1.0f );
			float weights[layers];
			for( uint32_t layer = 0; layer < layers; ++layer )
			{
				const std::vector<float>& w = masks.splat[layer];
				if( w.empty() )
				{
					weights[layer] = 0.0f;
					continue;
				}
				const auto at = [&]( int c, int r ) { return w[static_cast<size_t>( r ) * size + c]; };
				const float upper = at( c0, r0 ) + ( at( c1, r0 ) - at( c0, r0 ) ) * fu;
				const float lower = at( c0, r1 ) + ( at( c1, r1 ) - at( c0, r1 ) ) * fu;
				weights[layer] = upper + ( lower - upper ) * fv;
			}
			// Покраска ручными правками — поверх (TerrainMaterial: вес × remaining + покраска слоя)
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
			// Галька русел — по врезу в текселе (было в terrain.ps по врезу в точке)
			const auto paint = [&]( float share )
			{
				for( uint32_t layer = 0; layer < layers; ++layer )
					weights[layer] = weights[layer] * ( 1.0f - share ) + ( static_cast<int32_t>( layer ) == source.channelLayer ? share : 0.0f );
			};
			if( channels )
				paint( smoothstep( 0.03f, 0.15f, tile.carve[k] ) );
			for( uint32_t slice = 0; slice < slicesPerTile; ++slice )
			{
				uint8_t* texel = out + ( slice * texels + k ) * 4;
				for( uint32_t c = 0; c < 4; ++c )
					texel[c] = static_cast<uint8_t>( std::lround( std::clamp( weights[slice * 4 + c], 0.0f, 1.0f ) * 255.0f ) );
			}
		}
	}
}

bool TerrainDetailSplat::build( const std::vector<TerrainHydrology::DetailTile>& tiles, uint32_t tilesPerSide, const Source& source )
{
	const auto start = std::chrono::steady_clock::now();
	m_side = TerrainHydrology::detailTileCells * TerrainHydrology::detailSamples + 2;
	const bool any = !tiles.empty() && source.masks && source.masks->size > 0;
	m_tileSize = any ? TerrainHydrology::detailTileCells * source.texelSize : 0.0f;
	m_tileCount = any ? tiles.size() : 0;

	// Веса всех плиток — параллельно по плиткам; без плиток — два нулевых среза 1 × 1
	const size_t sliceBytes = any ? static_cast<size_t>( m_side ) * m_side * 4 : 4;
	const size_t slices = std::max<size_t>( m_tileCount, 1 ) * slicesPerTile;
	std::vector<uint8_t> weights( slices * sliceBytes, 0 );
	if( any )
	{
		std::vector<size_t> order( m_tileCount );
		std::iota( order.begin(), order.end(), size_t( 0 ) );
		std::for_each( std::execution::par, order.begin(), order.end(),
					   [&]( size_t t ) { computeTile( tiles[t], source, &weights[t * slicesPerTile * sliceBytes] ); } );
	}

	DMD3D& d3d = DMD3D::instance();
	TextureDesc desc;
	desc.width = desc.height = any ? m_side : 1;
	desc.arraySize = static_cast<uint32_t>( slices );
	desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	std::vector<TextureData> data( slices );
	for( size_t s = 0; s < slices; ++s )
	{
		data[s].data = &weights[s * sliceBytes];
		data[s].rowPitch = desc.width * 4;
	}
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	if( !d3d.createTexture( desc, data.data(), m_texture ) || !d3d.createShaderView( m_texture, arrayView, m_view ) )
	{
		LOG( "Terrain detail splat: the tile array is not created" );
		return false;
	}
	d3d.setName( m_texture, "Terrain detail splat" );

	// Индекс: плитка мира → номер + 1 (0 — нет); без плиток — 1 × 1 ноль
	const uint32_t indexSide = any ? tilesPerSide : 1;
	std::vector<uint16_t> index( static_cast<size_t>( indexSide ) * indexSide, 0 );
	for( size_t t = 0; t < m_tileCount; ++t )
		if( tiles[t].x < indexSide && tiles[t].z < indexSide )
			index[static_cast<size_t>( tiles[t].z ) * indexSide + tiles[t].x] = static_cast<uint16_t>( t + 1 );
	TextureDesc indexDesc;
	indexDesc.width = indexDesc.height = indexSide;
	indexDesc.format = DXGI_FORMAT_R16_UINT;
	TextureData indexData;
	indexData.data = index.data();
	indexData.rowPitch = indexSide * sizeof( uint16_t );
	if( !d3d.createTexture( indexDesc, &indexData, m_indexTexture ) || !d3d.createShaderView( m_indexTexture, {}, m_indexView ) )
	{
		LOG( "Terrain detail splat: the tile index is not created" );
		return false;
	}
	d3d.setName( m_indexTexture, "Terrain detail splat index" );
	if( any )
		LOG( "Terrain detail splat: tiles " + std::to_string( m_tileCount ) + " (" + std::to_string( weights.size() / 1024 / 1024 ) + " MB), ms: " +
			 std::to_string( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() ) );
	return true;
}

void TerrainDetailSplat::refresh( const std::vector<TerrainHydrology::DetailTile>& tiles, const Source& source, uint32_t x0, uint32_t row0,
								  uint32_t x1, uint32_t row1 )
{
	if( m_tileSize <= 0.0f || !source.masks || tiles.size() != m_tileCount || x0 >= x1 || row0 >= row1 )
		return;
	// Клетки — в мир (строка 0 — дальний край по z), с запасом в клетку: тексель плитки берёт четыре клетки вокруг
	const float worldSize = source.masks->size * source.texelSize;
	const float left = ( static_cast<float>( x0 ) - 1.0f ) * source.texelSize;
	const float right = ( static_cast<float>( x1 ) + 1.0f ) * source.texelSize;
	const float bottom = worldSize - ( static_cast<float>( row1 ) + 1.0f ) * source.texelSize;
	const float top = worldSize - ( static_cast<float>( row0 ) - 1.0f ) * source.texelSize;
	const float margin = m_tileSize / ( m_side - 2 );	// поле плитки
	std::vector<size_t> touched;
	for( size_t t = 0; t < m_tileCount; ++t )
	{
		const float tileLeft = tiles[t].x * m_tileSize - margin;
		const float tileNear = tiles[t].z * m_tileSize - margin;
		if( tileLeft < right && tileLeft + m_tileSize + 2.0f * margin > left && tileNear < top && tileNear + m_tileSize + 2.0f * margin > bottom )
			touched.push_back( t );
	}
	if( touched.empty() )
		return;
	const size_t sliceBytes = static_cast<size_t>( m_side ) * m_side * 4;
	std::vector<uint8_t> weights( touched.size() * slicesPerTile * sliceBytes );
	std::vector<size_t> order( touched.size() );
	std::iota( order.begin(), order.end(), size_t( 0 ) );
	std::for_each( std::execution::par, order.begin(), order.end(),
				   [&]( size_t n ) { computeTile( tiles[touched[n]], source, &weights[n * slicesPerTile * sliceBytes] ); } );
	for( size_t n = 0; n < touched.size(); ++n )
		for( uint32_t slice = 0; slice < slicesPerTile; ++slice )
		{
			TextureRegion region;
			region.slice = static_cast<uint32_t>( touched[n] ) * slicesPerTile + slice;
			DMD3D::instance().updateTexture( m_texture, region, &weights[( n * slicesPerTile + slice ) * sliceBytes], m_side * 4 );
		}
}

void TerrainDetailSplat::bind() const
{
	DMD3D::instance().setSRV( 5, m_view );
	DMD3D::instance().setSRV( 6, m_indexView );
}

}
