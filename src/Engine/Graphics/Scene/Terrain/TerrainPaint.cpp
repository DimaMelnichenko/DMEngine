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

void TerrainPaint::initialize( uint32_t size, float texel, const std::vector<TerrainPaintTile>& tiles )
{
	m_size = size;
	m_texel = texel;
	m_targets.clear();
	const int32_t tilesPerSide = static_cast<int32_t>( ( size + tileSize - 1 ) / tileSize );
	for( const TerrainPaintTile& tile : tiles )
	{
		if( tile.delta.size() != tileSize * tileSize || tile.tileX < 0 || tile.tileZ < 0 || tile.tileX >= tilesPerSide ||
			tile.tileZ >= tilesPerSide )
		{
			LOG( "Terrain paint: tile " + std::to_string( tile.tileX ) + ", " + std::to_string( tile.tileZ ) + " of " + tile.target +
				 " does not fit the " + std::to_string( size ) + " map, skipped" );
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

void TerrainPaint::beginStroke( const std::string& target, Mode mode, float value, const std::vector<float>* rules )
{
	m_stroke = Stroke();
	m_stroke.active = m_size != 0;
	m_stroke.target = target;
	m_stroke.mode = mode;
	m_stroke.value = value;
	m_stroke.rules = rules && rules->size() == static_cast<size_t>( m_size ) * m_size ? rules : nullptr;
	const auto found = m_targets.find( target );
	if( found != m_targets.end() )
		m_stroke.before = found->second;
	m_stroke.coverage.assign( static_cast<size_t>( m_size ) * m_size, 0.0f );
}

TerrainPaint::Rect TerrainPaint::dab( float x, float z, float radius, float falloff )
{
	Rect rect;
	if( !m_stroke.active || radius <= 0.0f )
		return rect;
	// Центр текселя (col, row): x = (col + 0,5)·texel, z = сторона − (row + 0,5)·texel
	const float world = m_size * m_texel;
	const float column = x / m_texel - 0.5f;
	const float row = ( world - z ) / m_texel - 0.5f;
	const float reach = radius / m_texel;
	const int32_t limit = static_cast<int32_t>( m_size );
	rect.x0 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::floor( column - reach ) ), 0, limit ) );
	rect.x1 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::ceil( column + reach ) ) + 1, 0, limit ) );
	rect.row0 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::floor( row - reach ) ), 0, limit ) );
	rect.row1 = static_cast<uint32_t>( std::clamp( static_cast<int32_t>( std::ceil( row + reach ) ) + 1, 0, limit ) );
	if( rect.empty() )
		return rect;

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
			const size_t i = static_cast<size_t>( r ) * m_size + c;
			if( weight <= m_stroke.coverage[i] )
				continue;
			m_stroke.coverage[i] = weight;

			const std::pair<int32_t, int32_t> key{ static_cast<int32_t>( c / tileSize ), static_cast<int32_t>( r / tileSize ) };
			const size_t inTile = ( r % tileSize ) * tileSize + c % tileSize;
			const auto before = m_stroke.before.find( key );
			const float start = before == m_stroke.before.end() ? 0.0f : decode( before->second[inTile] );
			float delta = start;
			switch( m_stroke.mode )
			{
			case Mode::value:
			{
				const float rule = m_stroke.rules ? ( *m_stroke.rules )[i] : 0.0f;
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

void TerrainPaint::applyMask( const std::string& target, const std::vector<float>& rules, std::vector<float>& out, const Rect& rect ) const
{
	const auto found = m_targets.find( target );
	if( found == m_targets.end() || rules.size() != static_cast<size_t>( m_size ) * m_size || out.size() != rules.size() )
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

void TerrainPaint::applyLayers( const std::vector<float>* rules, std::vector<float>* out, uint32_t layers, const Rect& rect ) const
{
	// Слои с мазками по порядку номеров и плитки, где есть хоть одна их поправка
	std::vector<std::pair<uint32_t, const Tiles*>> painted;
	std::map<std::pair<int32_t, int32_t>, bool> keys;
	for( const auto& [target, tiles] : m_targets )
	{
		const int32_t layer = layerOf( target );
		if( layer < 0 || static_cast<uint32_t>( layer ) >= layers || out[layer].size() != static_cast<size_t>( m_size ) * m_size )
			continue;
		painted.emplace_back( static_cast<uint32_t>( layer ), &tiles );
		for( const auto& [key, tile] : tiles )
			keys[key] = true;
	}
	std::sort( painted.begin(), painted.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );
	if( painted.empty() )
		return;

	for( const auto& [key, unused] : keys )
	{
		const uint32_t c0 = std::max( static_cast<uint32_t>( key.first ) * tileSize, rect.x0 );
		const uint32_t c1 = std::min( static_cast<uint32_t>( key.first + 1 ) * tileSize, rect.x1 );
		const uint32_t r0 = std::max( static_cast<uint32_t>( key.second ) * tileSize, rect.row0 );
		const uint32_t r1 = std::min( static_cast<uint32_t>( key.second + 1 ) * tileSize, rect.row1 );
		std::vector<const Tile*> tiles( painted.size(), nullptr );
		for( size_t p = 0; p < painted.size(); ++p )
		{
			const auto tile = painted[p].second->find( key );
			tiles[p] = tile == painted[p].second->end() ? nullptr : &tile->second;
		}
		for( uint32_t r = r0; r < r1; ++r )
			for( uint32_t c = c0; c < c1; ++c )
			{
				const size_t i = static_cast<size_t>( r ) * m_size + c;
				float weights[TerrainMasks::splatLayers] = {};
				for( uint32_t layer = 0; layer < layers; ++layer )
					weights[layer] = rules[layer].empty() ? 0.0f : rules[layer][i];
				for( size_t p = 0; p < painted.size(); ++p )
				{
					if( !tiles[p] )
						continue;
					const float d = decode( ( *tiles[p] )[( r % tileSize ) * tileSize + c % tileSize] );
					const uint32_t k = painted[p].first;
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
						else
						{
							for( uint32_t layer = 0; layer < layers; ++layer )
								if( layer != k && !out[layer].empty() )
								{
									weights[layer] += freed;
									break;
								}
						}
						weights[k] = kept;
					}
				}
				for( uint32_t layer = 0; layer < layers; ++layer )
					if( !out[layer].empty() )
						out[layer][i] = weights[layer];
			}
	}
}

}
