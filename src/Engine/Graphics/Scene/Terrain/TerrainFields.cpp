#include "TerrainFields.h"
#include <cmath>

namespace GS
{

float fieldSmoothstep( float a, float b, float x )
{
	const float t = std::clamp( ( x - a ) / ( b - a ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

TerrainField blurField( TerrainField values, uint32_t size, int radius, int passes )
{
	if( radius <= 0 || values.empty() )
		return values;
	const int n = static_cast<int>( size );
	const float scale = 1.0f / ( 2 * radius + 1 );
	TerrainField temp( values.size() );
	for( int pass = 0; pass < passes; ++pass )
	{
		// По строкам: скользящая сумма
		forRows( size, [&]( uint32_t row )
		{
			const float* in = &values[static_cast<size_t>( row ) * n];
			float* out = &temp[static_cast<size_t>( row ) * n];
			float sum = 0.0f;
			for( int k = -radius; k <= radius; ++k )
				sum += in[std::clamp( k, 0, n - 1 )];
			for( int col = 0; col < n; ++col )
			{
				out[col] = sum * scale;
				sum += in[std::min( col + radius + 1, n - 1 )] - in[std::max( col - radius, 0 )];
			}
		} );
		// По столбцам
		forRows( size, [&]( uint32_t col )
		{
			float sum = 0.0f;
			for( int k = -radius; k <= radius; ++k )
				sum += temp[static_cast<size_t>( std::clamp( k, 0, n - 1 ) ) * n + col];
			for( int row = 0; row < n; ++row )
			{
				values[static_cast<size_t>( row ) * n + col] = sum * scale;
				sum += temp[static_cast<size_t>( std::min( row + radius + 1, n - 1 ) ) * n + col] -
					   temp[static_cast<size_t>( std::max( row - radius, 0 ) ) * n + col];
			}
		} );
	}
	return values;
}

TerrainField upwindHorizon( const TerrainField& heights, uint32_t size, float texel, float heightScale, float windX, float windZ,
							float reach )
{
	TerrainField result( static_cast<size_t>( size ) * size, 0.0f );
	const float windLength = std::hypot( windX, windZ );
	if( windLength <= 1e-3f || heights.size() != result.size() )
		return result;
	const int n = static_cast<int>( size );
	auto heightAt = [&]( int row, int col )
	{
		return heights[static_cast<size_t>( std::clamp( row, 0, n - 1 ) ) * n + std::clamp( col, 0, n - 1 )];
	};

	// В текселях поля: x — столбцы, +z мира — к меньшим строкам; навстречу ветру — против направления
	const float upCol = -windX / windLength;
	const float upRow = windZ / windLength;
	struct Sample
	{
		int dCol;
		int dRow;
		float distance;		// м
	};
	constexpr int fan = 3;
	constexpr float fanAngle = 0.2618f;	// 15°
	std::vector<Sample> samples[fan];
	const float reachTexels = std::max( reach / texel, 2.0f );
	for( int f = 0; f < fan; ++f )
	{
		const float angle = ( f - ( fan - 1 ) * 0.5f ) * fanAngle;
		const float c = std::cos( angle );
		const float sn = std::sin( angle );
		const float dirCol = upCol * c - upRow * sn;
		const float dirRow = upCol * sn + upRow * c;
		for( float d = 1.5f; d <= reachTexels; d = std::max( d * 1.12f, d + 1.0f ) )
		{
			const int dCol = static_cast<int>( std::lround( dirCol * d ) );
			const int dRow = static_cast<int>( std::lround( dirRow * d ) );
			samples[f].push_back( { dCol, dRow, std::hypot( static_cast<float>( dCol ), static_cast<float>( dRow ) ) * texel } );
		}
	}
	forRows( size, [&]( uint32_t row )
	{
		const int r = static_cast<int>( row );
		for( int col = 0; col < n; ++col )
		{
			const float h = heights[static_cast<size_t>( r ) * n + col];
			float sx = 0.0f;
			for( int f = 0; f < fan; ++f )
			{
				float steepest = -1e9f;
				for( const Sample& sample : samples[f] )
					steepest = std::max( steepest, ( heightAt( r + sample.dRow, col + sample.dCol ) - h ) * heightScale / std::max( sample.distance, 1e-3f ) );
				sx += std::atan( steepest ) * 57.29578f;
			}
			result[static_cast<size_t>( r ) * n + col] = sx / fan;
		}
	} );
	return result;
}

}
