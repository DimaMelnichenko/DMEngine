#include "TerrainEdits.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace GS
{

namespace
{

// Точка кривой правки в мире, м
struct CurveSample
{
	float x = 0.0f;
	float z = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float falloff = 0.0f;
};

// Высота поля в точке мира, м: билинейно по центрам текселей
float fieldHeight( const HeightField& field, float x, float z )
{
	const float maxIndex = static_cast<float>( field.size - 1 );
	const float col = std::clamp( x / field.texelSize - 0.5f, 0.0f, maxIndex );
	const float row = std::clamp( field.size - z / field.texelSize - 0.5f, 0.0f, maxIndex );
	const uint32_t c0 = static_cast<uint32_t>( col );
	const uint32_t r0 = static_cast<uint32_t>( row );
	const uint32_t c1 = std::min( c0 + 1, field.size - 1 );
	const uint32_t r1 = std::min( r0 + 1, field.size - 1 );
	const float fc = col - c0;
	const float fr = row - r0;
	const auto at = [&field]( uint32_t r, uint32_t c ) { return field.heights[r * field.rowPitch + c]; };
	const float top = at( r0, c0 ) + ( at( r0, c1 ) - at( r0, c0 ) ) * fc;
	const float bottom = at( r1, c0 ) + ( at( r1, c1 ) - at( r1, c0 ) ) * fc;
	return ( top + ( bottom - top ) * fr ) * field.heightMultiplier + field.heightOffset;
}

float catmullRom( float p0, float p1, float p2, float p3, float t )
{
	return 0.5f * ( 2.0f * p1 + ( p2 - p0 ) * t + ( 2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 ) * t * t +
					( 3.0f * p1 - p0 - 3.0f * p2 + p3 ) * t * t * t );
}

// Кривая правки — точки не реже половины текселя: Catmull-Rom через опорные (концы повторяются) или ломаная; ширина и
// полоса перехода — линейно между опорными. Высота относительной правки — от рельефа под опорной точкой
std::vector<CurveSample> buildCurve( const TerrainEdit& edit, const HeightField& field )
{
	std::vector<CurveSample> control;
	for( const TerrainEditPoint& point : edit.points )
	{
		CurveSample sample;
		sample.x = point.position.x;
		sample.z = point.position.z;
		sample.y = edit.relative ? fieldHeight( field, point.position.x, point.position.z ) + point.position.y : point.position.y;
		sample.width = std::max( point.width, 0.0f );
		sample.falloff = std::max( point.falloff, 0.0f );
		control.push_back( sample );
	}
	if( control.size() < 2 )
		return control;

	std::vector<CurveSample> curve;
	const size_t last = control.size() - 1;
	for( size_t i = 0; i < last; ++i )
	{
		const CurveSample& p0 = control[i > 0 ? i - 1 : 0];
		const CurveSample& p1 = control[i];
		const CurveSample& p2 = control[i + 1];
		const CurveSample& p3 = control[std::min( i + 2, last )];
		const float length = std::hypot( p2.x - p1.x, p2.z - p1.z );
		const int steps = std::max( 1, static_cast<int>( std::ceil( length / ( field.texelSize * 0.5f ) ) ) );
		for( int s = 0; s < steps; ++s )
		{
			const float t = static_cast<float>( s ) / steps;
			CurveSample sample;
			if( edit.smooth )
			{
				sample.x = catmullRom( p0.x, p1.x, p2.x, p3.x, t );
				sample.z = catmullRom( p0.z, p1.z, p2.z, p3.z, t );
				sample.y = catmullRom( p0.y, p1.y, p2.y, p3.y, t );
			}
			else
			{
				sample.x = p1.x + ( p2.x - p1.x ) * t;
				sample.z = p1.z + ( p2.z - p1.z ) * t;
				sample.y = p1.y + ( p2.y - p1.y ) * t;
			}
			sample.width = p1.width + ( p2.width - p1.width ) * t;
			sample.falloff = p1.falloff + ( p2.falloff - p1.falloff ) * t;
			curve.push_back( sample );
		}
	}
	curve.push_back( control.back() );
	return curve;
}

}

float TerrainEditCoverage::sample( const std::vector<float>& values, float u, float v ) const
{
	if( values.empty() || size == 0 )
		return 0.0f;
	const float maxIndex = static_cast<float>( size - 1 );
	const float col = std::clamp( u * size - 0.5f, 0.0f, maxIndex );
	const float row = std::clamp( v * size - 0.5f, 0.0f, maxIndex );
	const uint32_t c0 = static_cast<uint32_t>( col );
	const uint32_t r0 = static_cast<uint32_t>( row );
	const uint32_t c1 = std::min( c0 + 1, size - 1 );
	const uint32_t r1 = std::min( r0 + 1, size - 1 );
	const float fc = col - c0;
	const float fr = row - r0;
	const auto at = [this, &values]( uint32_t r, uint32_t c ) { return values[static_cast<size_t>( r ) * size + c]; };
	const float top = at( r0, c0 ) + ( at( r0, c1 ) - at( r0, c0 ) ) * fc;
	const float bottom = at( r1, c0 ) + ( at( r1, c1 ) - at( r1, c0 ) ) * fc;
	return top + ( bottom - top ) * fr;
}

size_t applyTerrainEdits( HeightField& field, const std::vector<TerrainEdit>& edits, TerrainEditCoverage* coverage )
{
	if( !field.heights || field.size == 0 || field.heightMultiplier == 0.0f )
		return 0;

	// Ближайшая точка кривой у каждого текселя, которого кривая достаёт: расстояние, высота, половина ширины, полоса
	const size_t texelCount = static_cast<size_t>( field.size ) * field.size;
	std::vector<float> distance( texelCount, FLT_MAX );
	std::vector<CurveSample> nearest( texelCount );
	std::vector<uint32_t> touched;
	size_t changed = 0;
	if( coverage )
		coverage->size = field.size;

	for( const TerrainEdit& edit : edits )
	{
		const bool height = edit.raise || edit.lower;
		const bool clear = coverage && edit.clearFoliage > 0.0f;
		const bool paint = coverage && edit.paintLayer >= 0 && edit.paintLayer < static_cast<int>( TerrainEditCoverage::paintLayers );
		if( !height && !clear && !paint )
			continue;
		if( clear && coverage->foliageClear.empty() )
			coverage->foliageClear.assign( texelCount, 0.0f );
		if( paint )
		{
			if( coverage->remaining.empty() )
				coverage->remaining.assign( texelCount, 1.0f );
			if( coverage->paint[edit.paintLayer].empty() )
				coverage->paint[edit.paintLayer].assign( texelCount, 0.0f );
		}
		const std::vector<CurveSample> curve = buildCurve( edit, field );
		if( curve.empty() )
			continue;

		// Отрезки кривой (у одной точки — вырожденный): каждый обходит свои тексели в пределах досягаемости
		const size_t segments = curve.size() > 1 ? curve.size() - 1 : 1;
		for( size_t i = 0; i < segments; ++i )
		{
			const CurveSample& a = curve[i];
			const CurveSample& b = curve[std::min( i + 1, curve.size() - 1 )];
			const float reach = std::max( a.width, b.width ) * 0.5f + std::max( a.falloff, b.falloff ) + field.texelSize;
			const float minX = std::min( a.x, b.x ) - reach;
			const float maxX = std::max( a.x, b.x ) + reach;
			const float minZ = std::min( a.z, b.z ) - reach;
			const float maxZ = std::max( a.z, b.z ) + reach;
			const int colMin = std::max( static_cast<int>( std::floor( minX / field.texelSize - 0.5f ) ), 0 );
			const int colMax = std::min( static_cast<int>( std::ceil( maxX / field.texelSize - 0.5f ) ), static_cast<int>( field.size ) - 1 );
			const int rowMin = std::max( static_cast<int>( std::floor( field.size - maxZ / field.texelSize - 0.5f ) ), 0 );
			const int rowMax = std::min( static_cast<int>( std::ceil( field.size - minZ / field.texelSize - 0.5f ) ), static_cast<int>( field.size ) - 1 );
			const float dx = b.x - a.x;
			const float dz = b.z - a.z;
			const float lengthSq = dx * dx + dz * dz;
			for( int row = rowMin; row <= rowMax; ++row )
			{
				const float z = ( field.size - row - 0.5f ) * field.texelSize;
				for( int col = colMin; col <= colMax; ++col )
				{
					const float x = ( col + 0.5f ) * field.texelSize;
					const float t = lengthSq > 0.0f ? std::clamp( ( ( x - a.x ) * dx + ( z - a.z ) * dz ) / lengthSq, 0.0f, 1.0f ) : 0.0f;
					const float qx = a.x + dx * t;
					const float qz = a.z + dz * t;
					const float d = std::hypot( x - qx, z - qz );
					const size_t index = static_cast<size_t>( row ) * field.size + col;
					if( d >= distance[index] )
						continue;
					if( distance[index] == FLT_MAX )
						touched.push_back( static_cast<uint32_t>( index ) );
					distance[index] = d;
					CurveSample& sample = nearest[index];
					sample.y = a.y + ( b.y - a.y ) * t;
					sample.width = a.width + ( b.width - a.width ) * t;
					sample.falloff = a.falloff + ( b.falloff - a.falloff ) * t;
				}
			}
		}

		// Наложение: в пределах половины ширины — к высоте кривой, в полосе перехода — косинусом к исходному
		for( uint32_t index : touched )
		{
			const CurveSample& sample = nearest[index];
			const float half = sample.width * 0.5f;
			const float d = distance[index];
			distance[index] = FLT_MAX;
			float weight = 1.0f;
			if( d > half )
			{
				if( sample.falloff <= 0.0f || d >= half + sample.falloff )
					continue;
				weight = 0.5f + 0.5f * std::cos( 3.14159265f * ( d - half ) / sample.falloff );
			}
			if( clear )
				coverage->foliageClear[index] = std::max( coverage->foliageClear[index], weight * edit.clearFoliage );
			if( paint )
			{
				coverage->remaining[index] *= 1.0f - weight;
				for( std::vector<float>& layer : coverage->paint )
					if( !layer.empty() )
						layer[index] *= 1.0f - weight;
				coverage->paint[edit.paintLayer][index] += weight;
			}
			if( !height )
				continue;
			const uint32_t row = index / field.size;
			const uint32_t col = index % field.size;
			float& value = field.heights[row * field.rowPitch + col];
			const float height = value * field.heightMultiplier + field.heightOffset;
			float target = sample.y;
			if( !edit.lower )
				target = std::max( height, target );
			if( !edit.raise )
				target = std::min( height, target );
			const float edited = height + ( target - height ) * weight;
			const float normalized = std::clamp( ( edited - field.heightOffset ) / field.heightMultiplier, 0.0f, 1.0f );
			if( normalized != value )
				++changed;
			value = normalized;
		}
		touched.clear();
	}
	return changed;
}

}
