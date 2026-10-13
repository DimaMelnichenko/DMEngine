#include "TerrainPaint.h"
#include <algorithm>
#include <cmath>
#include "TerrainMasks.h"
#include "Logger\Logger.h"

namespace GS
{

void TerrainPaint::Rect::add( const Rect& other )
{
	if( other.empty() )
		return;
	if( empty() )
	{
		*this = other;
		return;
	}
	x0 = std::min( x0, other.x0 );
	row0 = std::min( row0, other.row0 );
	x1 = std::max( x1, other.x1 );
	row1 = std::max( row1, other.row1 );
}

int32_t TerrainPaint::layerOf( const std::string& target )
{
	if( target.rfind( "layer ", 0 ) != 0 )
		return -1;
	try
	{
		return std::stoi( target.substr( 6 ) );
	}
	catch( const std::exception& )
	{
		return -1;
	}
}

int8_t TerrainPaint::encode( float value )
{
	return static_cast<int8_t>( std::lround( std::clamp( value, -1.0f, 1.0f ) * 127.0f ) );
}

float TerrainPaint::result( bool layer, float rule, float delta )
{
	if( !layer )
		return std::clamp( rule + delta, 0.0f, 1.0f );
	return delta >= 0.0f ? rule + delta * ( 1.0f - rule ) : rule * ( 1.0f + delta );
}

float TerrainPaint::deltaFor( bool layer, float rule, float value )
{
	if( !layer )
		return std::clamp( value - rule, -1.0f, 1.0f );
	if( value >= rule )
		return rule < 1.0f ? std::clamp( ( value - rule ) / ( 1.0f - rule ), 0.0f, 1.0f ) : 0.0f;
	return rule > 0.0f ? std::clamp( value / rule - 1.0f, -1.0f, 0.0f ) : 0.0f;
}

void TerrainPaint::applyLayerDelta( float* weights, uint32_t layers, uint32_t k, float d )
{
	if( k >= layers )
		return;
	if( d > 0.0f )
	{
		// К «только этот слой»: все × (1 − d), слою — ещё d
		for( uint32_t layer = 0; layer < layers; ++layer )
			weights[layer] *= 1.0f - d;
		weights[k] += d;
	}
	else if( d < 0.0f )
	{
		// Слою — × (1 + d), освободившееся — остальным пропорционально; остальных нет — первому другому слою
		const float kept = weights[k] * ( 1.0f + d );
		const float freed = weights[k] - kept;
		float others = 0.0f;
		for( uint32_t layer = 0; layer < layers; ++layer )
			if( layer != k )
				others += weights[layer];
		if( others > 1e-6f )
		{
			const float scale = ( others + freed ) / others;
			for( uint32_t layer = 0; layer < layers; ++layer )
				if( layer != k )
					weights[layer] *= scale;
		}
		else if( layers > 1 )
			weights[k == 0 ? 1 : 0] += freed;
		weights[k] = kept;
	}
}

void TerrainPaint::initialize( uint32_t size, float texel, const std::vector<TerrainPaintTile>& tiles )
{
	m_size = size;
	m_texel = texel;
	m_targets.clear();
	for( const TerrainPaintTile& tile : tiles )
	{
		const int32_t tilesPerSide = static_cast<int32_t>( ( gridSize( tile.target ) + tileSize - 1 ) / tileSize );
		if( tile.delta.size() != tileSize * tileSize || tile.tileX < 0 || tile.tileZ < 0 || tile.tileX >= tilesPerSide ||
			tile.tileZ >= tilesPerSide )
		{
			LOG( "Terrain paint: tile " + std::to_string( tile.tileX ) + ", " + std::to_string( tile.tileZ ) + " of " + tile.target +
				 " does not fit the " + std::to_string( gridSize( tile.target ) ) + " grid, skipped" );
			continue;
		}
		m_targets[tile.target][{ tile.tileX, tile.tileZ }] = tile.delta;
	}
}

std::vector<TerrainPaintTile> TerrainPaint::tiles() const
{
	std::vector<TerrainPaintTile> result;
	for( const auto& [target, tiles] : m_targets )
		for( const auto& [key, delta] : tiles )
			result.push_back( { target, key.first, key.second, delta } );
	return result;
}

std::vector<std::pair<std::string, size_t>> TerrainPaint::targets() const
{
	std::vector<std::pair<std::string, size_t>> result;
	for( const auto& [target, tiles] : m_targets )
		result.emplace_back( target, tiles.size() );
	return result;
}

bool TerrainPaint::has( const std::string& target ) const
{
	const auto found = m_targets.find( target );
	return found != m_targets.end() && !found->second.empty();
}

const TerrainPaint::Tile* TerrainPaint::findTile( const std::string& target, uint32_t x, uint32_t row ) const
{
	const auto found = m_targets.find( target );
	if( found == m_targets.end() )
		return nullptr;
	const auto tile = found->second.find( { static_cast<int32_t>( x / tileSize ), static_cast<int32_t>( row / tileSize ) } );
	return tile == found->second.end() ? nullptr : &tile->second;
}

float TerrainPaint::delta( const std::string& target, uint32_t x, uint32_t row ) const
{
	const Tile* tile = findTile( target, x, row );
	return tile ? decode( ( *tile )[( row % tileSize ) * tileSize + x % tileSize] ) : 0.0f;
}

bool TerrainPaint::deltas( const std::string& target, int32_t x0, int32_t row0, uint32_t width, uint32_t height, std::vector<float>& out ) const
{
	const auto found = m_targets.find( target );
	if( found == m_targets.end() || found->second.empty() )
		return false;
	const int32_t size = static_cast<int32_t>( gridSize( target ) );
	const int32_t x1 = std::min( x0 + static_cast<int32_t>( width ), size );
	const int32_t row1 = std::min( row0 + static_cast<int32_t>( height ), size );
	const int32_t cx0 = std::max( x0, 0 );
	const int32_t cr0 = std::max( row0, 0 );
	if( cx0 >= x1 || cr0 >= row1 )
		return false;
	// Плитки, которые задевает прямоугольник, — по одной выборке из карты на плитку
	const int32_t t = static_cast<int32_t>( tileSize );
	bool any = false;
	for( int32_t tz = cr0 / t; tz <= ( row1 - 1 ) / t; ++tz )
		for( int32_t tx = cx0 / t; tx <= ( x1 - 1 ) / t; ++tx )
		{
			const auto tile = found->second.find( { tx, tz } );
			if( tile == found->second.end() )
				continue;
			if( !any )
				out.assign( static_cast<size_t>( width ) * height, 0.0f );
			any = true;
			for( int32_t r = std::max( tz * t, cr0 ); r < std::min( ( tz + 1 ) * t, row1 ); ++r )
				for( int32_t c = std::max( tx * t, cx0 ); c < std::min( ( tx + 1 ) * t, x1 ); ++c )
					out[static_cast<size_t>( r - row0 ) * width + ( c - x0 )] = decode( tile->second[( r - tz * t ) * t + c - tx * t] );
		}
	return any;
}

void TerrainPaint::beginStroke( const std::string& target, Mode mode, float value, Rule rule )
{
	m_stroke = Stroke();
	m_stroke.active = m_size != 0;
	m_stroke.target = target;
	m_stroke.mode = mode;
	m_stroke.value = value;
	m_stroke.rule = std::move( rule );
	m_stroke.size = gridSize( target );
	m_stroke.texel = gridTexel( target );
	const auto found = m_targets.find( target );
	if( found != m_targets.end() )
		m_stroke.before = found->second;
}

TerrainPaint::Rect TerrainPaint::dabRect( const std::string& target, float x, float z, float radius ) const
{
	Rect rect;
	if( m_size == 0 || radius <= 0.0f )
		return rect;
	// Центр текселя (col, row): x = (col + 0,5)·texel, z = сторона − (row + 0,5)·texel
	const uint32_t size = gridSize( target );
	const float texel = gridTexel( target );
	const float column = x / texel - 0.5f;
	const float row = ( size * texel - z ) / texel - 0.5f;
	const float reach = radius / texel;
	const int32_t limit = static_cast<int32_t>( size );
	rect.x0 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::floor( column - reach ) ), 0, limit ) );
	rect.x1 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::ceil( column + reach ) ) + 1, 0, limit ) );
	rect.row0 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::floor( row - reach ) ), 0, limit ) );
	rect.row1 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::ceil( row + reach ) ) + 1, 0, limit ) );
	return rect;
}

TerrainPaint::Rect TerrainPaint::dab( float x, float z, float radius, float falloff )
{
	if( !m_stroke.active )
		return {};
	const Rect rect = dabRect( m_stroke.target, x, z, radius );
	if( rect.empty() )
		return {};
	const float column = x / m_stroke.texel - 0.5f;
	const float row = ( m_stroke.size * m_stroke.texel - z ) / m_stroke.texel - 0.5f;
	const float reach = radius / m_stroke.texel;

	const bool layer = layerOf( m_stroke.target ) >= 0;
	const float soft = std::clamp( falloff, 0.0f, 1.0f );
	Tiles& tiles = m_targets[m_stroke.target];
	Rect changed;
	for( uint32_t r = rect.row0; r < rect.row1; ++r )
		for( uint32_t c = rect.x0; c < rect.x1; ++c )
		{
			// Вес кисти: 1 до (1 − falloff) радиуса, к краю — плавно до 0
			const float t = std::hypot( c - column, r - row ) / reach;
			if( t >= 1.0f )
				continue;
			float weight = 1.0f;
			if( soft > 0.0f && t > 1.0f - soft )
			{
				const float s = ( 1.0f - t ) / soft;
				weight = s * s * ( 3.0f - 2.0f * s );
			}
			const Key key{ static_cast<int32_t>( c / tileSize ), static_cast<int32_t>( r / tileSize ) };
			const size_t inTile = ( r % tileSize ) * tileSize + c % tileSize;
			std::vector<float>& coverage = m_stroke.coverage[key];
			if( coverage.empty() )
				coverage.assign( tileSize * tileSize, 0.0f );
			if( weight <= coverage[inTile] )
				continue;
			coverage[inTile] = weight;

			const auto before = m_stroke.before.find( key );
			const float start = before == m_stroke.before.end() ? 0.0f : decode( before->second[inTile] );
			float delta = start;
			switch( m_stroke.mode )
			{
			case Mode::value:
			{
				const float rule = m_stroke.rule ? m_stroke.rule( c, r ) : 0.0f;
				const float from = result( layer, rule, start );
				delta = deltaFor( layer, rule, from + ( m_stroke.value - from ) * weight );
				break;
			}
			case Mode::step:
				delta = start + m_stroke.value * weight;
				break;
			case Mode::unstep:
				delta = start - m_stroke.value * weight;
				break;
			case Mode::erase:
				delta = start * ( 1.0f - weight );
				break;
			}
			Tile& tile = tiles[key];
			if( tile.empty() )
				tile.assign( tileSize * tileSize, 0 );
			tile[inTile] = encode( delta );
			changed.add( Rect{ c, r, c + 1, r + 1 } );
		}
	m_stroke.touched.add( changed );
	return changed;
}

TerrainPaint::Rect TerrainPaint::endStroke()
{
	const Rect touched = m_stroke.touched;
	// Плитки, где поправок не осталось, — не хранить
	const auto found = m_targets.find( m_stroke.target );
	if( found != m_targets.end() )
	{
		for( auto tile = found->second.begin(); tile != found->second.end(); )
		{
			if( std::all_of( tile->second.begin(), tile->second.end(), []( int8_t value ) { return value == 0; } ) )
				tile = found->second.erase( tile );
			else
				++tile;
		}
		if( found->second.empty() )
			m_targets.erase( found );
	}
	m_stroke = Stroke();
	return touched;
}

void TerrainPaint::clear( const std::string& target )
{
	m_targets.erase( target );
}

TerrainPaint::TileMap TerrainPaint::targetTiles( const std::string& target ) const
{
	const auto found = m_targets.find( target );
	return found == m_targets.end() ? TileMap() : found->second;
}

void TerrainPaint::setTargetTiles( const std::string& target, const TileMap& tiles )
{
	if( tiles.empty() )
		m_targets.erase( target );
	else
		m_targets[target] = tiles;
}

TerrainPaint::Rect TerrainPaint::tilesRect( const std::string& target, const TileMap& tiles ) const
{
	const uint32_t size = gridSize( target );
	Rect rect;
	for( const auto& [key, tile] : tiles )
		rect.add( Rect{ std::min( key.first * tileSize, size ), std::min( key.second * tileSize, size ),
						std::min( ( key.first + 1 ) * tileSize, size ), std::min( ( key.second + 1 ) * tileSize, size ) } );
	return rect;
}

void TerrainPaint::applyMask( const std::string& target, const std::vector<float>& rules, std::vector<float>& out, const Rect& rect ) const
{
	const auto found = m_targets.find( target );
	if( found == m_targets.end() || isFine( target ) || rules.size() != static_cast<size_t>( m_size ) * m_size || out.size() != rules.size() )
		return;
	for( const auto& [key, tile] : found->second )
	{
		const uint32_t c0 = std::max( static_cast<uint32_t>( key.first ) * tileSize, rect.x0 );
		const uint32_t c1 = std::min( static_cast<uint32_t>( key.first + 1 ) * tileSize, rect.x1 );
		const uint32_t r0 = std::max( static_cast<uint32_t>( key.second ) * tileSize, rect.row0 );
		const uint32_t r1 = std::min( static_cast<uint32_t>( key.second + 1 ) * tileSize, rect.row1 );
		for( uint32_t r = r0; r < r1; ++r )
			for( uint32_t c = c0; c < c1; ++c )
			{
				const size_t i = static_cast<size_t>( r ) * m_size + c;
				out[i] = result( false, rules[i], decode( tile[( r % tileSize ) * tileSize + c % tileSize] ) );
			}
	}
}

void TerrainPaint::applyFineAverage( std::vector<float>* weights, uint32_t layers, std::vector<float>& wetness, const Rect& rect ) const
{
	// Цели тонкой сетки: слои по порядку номеров, мокрость; плитки, где есть хоть одна их поправка
	std::vector<std::pair<uint32_t, const Tiles*>> painted;
	const Tiles* wet = nullptr;
	std::map<Key, bool> keys;
	const size_t cells = static_cast<size_t>( m_size ) * m_size;
	for( const auto& [target, tiles] : m_targets )
	{
		const int32_t layer = layerOf( target );
		if( target == wetnessTarget )
		{
			wet = &tiles;
			if( wetness.size() != cells )
				wetness.assign( cells, 0.0f );
		}
		else if( layer >= 0 && static_cast<uint32_t>( layer ) < layers )
		{
			painted.emplace_back( static_cast<uint32_t>( layer ), &tiles );
			if( weights[layer].size() != cells )
				weights[layer].assign( cells, 0.0f );
		}
		else
			continue;
		for( const auto& [key, tile] : tiles )
			keys[key] = true;
	}
	std::sort( painted.begin(), painted.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );
	if( keys.empty() )
		return;

	// Плитка тонкой сетки — tileSize / fineScale клеток масок по стороне, клетка — fineScale² её текселей
	const uint32_t cellsPerTile = tileSize / fineScale;
	const float share = 1.0f / ( fineScale * fineScale );
	std::vector<const Tile*> tiles( painted.size() );
	for( const auto& [key, unused] : keys )
	{
		for( size_t p = 0; p < painted.size(); ++p )
		{
			const auto tile = painted[p].second->find( key );
			tiles[p] = tile == painted[p].second->end() ? nullptr : &tile->second;
		}
		const Tile* wetTile = nullptr;
		if( wet )
			if( const auto tile = wet->find( key ); tile != wet->end() )
				wetTile = &tile->second;
		const uint32_t c0 = std::max( static_cast<uint32_t>( key.first ) * cellsPerTile, rect.x0 );
		const uint32_t c1 = std::min( static_cast<uint32_t>( key.first + 1 ) * cellsPerTile, std::min( rect.x1, m_size ) );
		const uint32_t r0 = std::max( static_cast<uint32_t>( key.second ) * cellsPerTile, rect.row0 );
		const uint32_t r1 = std::min( static_cast<uint32_t>( key.second + 1 ) * cellsPerTile, std::min( rect.row1, m_size ) );
		for( uint32_t r = r0; r < r1; ++r )
			for( uint32_t c = c0; c < c1; ++c )
			{
				const size_t i = static_cast<size_t>( r ) * m_size + c;
				float base[TerrainMasks::splatLayers] = {};
				for( uint32_t layer = 0; layer < layers; ++layer )
					base[layer] = weights[layer].empty() ? 0.0f : weights[layer][i];
				float sum[TerrainMasks::splatLayers] = {};
				float wetSum = 0.0f;
				for( uint32_t sy = 0; sy < fineScale; ++sy )
					for( uint32_t sx = 0; sx < fineScale; ++sx )
					{
						const uint32_t inTile = ( ( r - key.second * cellsPerTile ) * fineScale + sy ) * tileSize +
												( c - key.first * cellsPerTile ) * fineScale + sx;
						float w[TerrainMasks::splatLayers];
						std::copy( base, base + TerrainMasks::splatLayers, w );
						for( size_t p = 0; p < painted.size(); ++p )
							if( tiles[p] )
								applyLayerDelta( w, layers, painted[p].first, decode( ( *tiles[p] )[inTile] ) );
						for( uint32_t layer = 0; layer < layers; ++layer )
							sum[layer] += w[layer];
						if( wetTile )
							wetSum += std::max( decode( ( *wetTile )[inTile] ), 0.0f );
					}
				for( uint32_t layer = 0; layer < layers; ++layer )
					if( !weights[layer].empty() )
						weights[layer][i] = sum[layer] * share;
				if( wetTile )
					wetness[i] = wetSum * share;
			}
	}
}

}
