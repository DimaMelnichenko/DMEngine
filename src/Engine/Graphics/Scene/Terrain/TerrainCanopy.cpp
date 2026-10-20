#include "TerrainCanopy.h"
#include <algorithm>
#include <cmath>

namespace GS
{

uint32_t TerrainCanopy::mapSize( float worldSize )
{
	const float cells = std::ceil( std::max( worldSize, 0.0f ) / texel );
	return static_cast<uint32_t>( std::clamp( cells, 1.0f, static_cast<float>( maxSize ) ) );
}

std::vector<uint8_t> TerrainCanopy::buildWithBroadleaf( const std::vector<CanopyStamp>& stamps, uint32_t size, float worldSize )
{
	std::vector<CanopyStamp> broadleaf;
	for( const CanopyStamp& stamp : stamps )
		if( stamp.broadleaf )
			broadleaf.push_back( stamp );
	const std::vector<uint8_t> all = build( stamps, size, worldSize );
	const std::vector<uint8_t> leaves = broadleaf.empty() ? std::vector<uint8_t>( all.size(), 0 ) : build( broadleaf, size, worldSize );
	std::vector<uint8_t> map( all.size() * 2 );
	for( size_t i = 0; i < all.size(); ++i )
	{
		map[2 * i] = all[i];
		map[2 * i + 1] = leaves[i];
	}
	return map;
}

float TerrainCanopy::cover( float distance, float radius )
{
	if( radius <= 0.0f || distance >= radius )
		return 0.0f;
	const float share = distance / radius;
	if( share <= innerShare )
		return 1.0f;
	// Край кроны — плавно (smoothstep): тень редеет к краю ветвей
	const float t = ( share - innerShare ) / ( 1.0f - innerShare );
	return 1.0f - t * t * ( 3.0f - 2.0f * t );
}

std::vector<uint8_t> TerrainCanopy::build( const std::vector<CanopyStamp>& stamps, uint32_t size, float worldSize )
{
	std::vector<uint8_t> map( static_cast<size_t>( size ) * size, 0 );
	if( size == 0 || worldSize <= 0.0f )
		return map;
	const float cell = worldSize / size;
	const int32_t last = static_cast<int32_t>( size ) - 1;
	for( const CanopyStamp& stamp : stamps )
	{
		if( stamp.radius <= 0.0f )
			continue;
		// Тексели, центры которых могут попасть в круг: столбец — по x, строка — от дальнего края по z
		const int32_t col0 = std::max( static_cast<int32_t>( std::floor( ( stamp.x - stamp.radius ) / cell ) ), 0 );
		const int32_t col1 = std::min( static_cast<int32_t>( std::floor( ( stamp.x + stamp.radius ) / cell ) ), last );
		const int32_t row0 = std::max( static_cast<int32_t>( std::floor( ( worldSize - stamp.z - stamp.radius ) / cell ) ), 0 );
		const int32_t row1 = std::min( static_cast<int32_t>( std::floor( ( worldSize - stamp.z + stamp.radius ) / cell ) ), last );
		for( int32_t row = row0; row <= row1; ++row )
		{
			const float dz = worldSize - ( row + 0.5f ) * cell - stamp.z;
			for( int32_t col = col0; col <= col1; ++col )
			{
				const float dx = ( col + 0.5f ) * cell - stamp.x;
				const float value = cover( std::sqrt( dx * dx + dz * dz ), stamp.radius );
				if( value <= 0.0f )
					continue;
				uint8_t& texel = map[static_cast<size_t>( row ) * size + col];
				texel = std::max( texel, static_cast<uint8_t>( std::lround( value * 255.0f ) ) );
			}
		}
	}
	return map;
}

float TerrainCanopy::sample( const std::vector<uint8_t>& map, uint32_t size, float worldSize, float x, float z )
{
	if( size == 0 || worldSize <= 0.0f || map.size() < static_cast<size_t>( size ) * size )
		return 0.0f;
	const float cell = worldSize / size;
	const int32_t col = static_cast<int32_t>( std::floor( x / cell ) );
	const int32_t row = static_cast<int32_t>( std::floor( ( worldSize - z ) / cell ) );
	if( col < 0 || row < 0 || col >= static_cast<int32_t>( size ) || row >= static_cast<int32_t>( size ) )
		return 0.0f;
	return map[static_cast<size_t>( row ) * size + col] / 255.0f;
}

}
