// Рельеф на CPU: правки рельефа (TerrainEdits) и русла по стоку (TerrainHydrology) на маленьких картах
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "Terrain\TerrainEdits.h"
#include "Terrain\TerrainHydrology.h"

using namespace DirectX;
using namespace GS;

namespace
{

// Карта высот в памяти: нормированные высоты, высота, м = значение × multiplier; строка 0 — дальний край по z
struct TestField
{
	std::vector<float> heights;
	HeightField field;

	TestField( uint32_t size, float texel, float multiplier )
	{
		heights.assign( static_cast<size_t>( size ) * size, 0.0f );
		field.heights = heights.data();
		field.size = size;
		field.rowPitch = size;
		field.texelSize = texel;
		field.heightMultiplier = multiplier;
		field.heightOffset = 0.0f;
	}
	// Середина клетки в мире
	float x( uint32_t column ) const { return ( column + 0.5f ) * field.texelSize; }
	float z( uint32_t row ) const { return ( field.size - row - 0.5f ) * field.texelSize; }
	float& at( uint32_t row, uint32_t column ) { return heights[static_cast<size_t>( row ) * field.size + column]; }
	float metres( uint32_t row, uint32_t column ) { return at( row, column ) * field.heightMultiplier; }
};

TerrainEdit platform( float x, float height, float z, float width, float falloff )
{
	TerrainEdit edit;
	edit.name = "platform";
	TerrainEditPoint point;
	point.position = XMFLOAT3( x, height, z );
	point.width = width;
	point.falloff = falloff;
	edit.points.push_back( point );
	return edit;
}

// V-долина: дно по x = середине, уклон вниз к z = 0 (к строке size − 1)
TestField valley( uint32_t size, float texel )
{
	TestField terrain( size, texel, 200.0f );
	const float middle = 0.5f * size * texel;
	for( uint32_t row = 0; row < size; ++row )
		for( uint32_t column = 0; column < size; ++column )
			terrain.at( row, column ) = ( 50.0f + 0.08f * std::abs( terrain.x( column ) - middle ) + 0.05f * terrain.z( row ) ) / 200.0f;
	return terrain;
}

}

TEST_CASE( "TerrainEdits: a platform levels the ground inside its width and blends in the falloff" )
{
	TestField terrain( 64, 1.0f, 100.0f );
	std::fill( terrain.heights.begin(), terrain.heights.end(), 0.1f );		// 10 м
	const std::vector<TerrainEdit> edits = { platform( 32.0f, 20.0f, 32.0f, 8.0f, 8.0f ) };
	CHECK( applyTerrainEdits( terrain.field, edits ) > 0 );
	CHECK( terrain.metres( 31, 31 ) == doctest::Approx( 20.0f ).epsilon( 0.01 ) );	// середина — высота площадки
	CHECK( terrain.metres( 0, 0 ) == doctest::Approx( 10.0f ) );					// далеко — как было
	// В полосе перехода (6 м от середины: за половиной ширины 4 м, до края 4 + 8 м) — между
	const float blend = terrain.metres( 31, 37 );
	CHECK( blend > 10.5f );
	CHECK( blend < 19.5f );
}

TEST_CASE( "TerrainEdits: raise and lower limit the direction of the edit" )
{
	TestField terrain( 32, 1.0f, 100.0f );
	std::fill( terrain.heights.begin(), terrain.heights.end(), 0.1f );
	TerrainEdit onlyLower = platform( 16.0f, 20.0f, 16.0f, 8.0f, 2.0f );
	onlyLower.raise = false;
	applyTerrainEdits( terrain.field, { onlyLower } );
	CHECK( terrain.metres( 15, 15 ) == doctest::Approx( 10.0f ) );		// поднимать нельзя — не поднялось

	TerrainEdit dig = platform( 16.0f, 5.0f, 16.0f, 8.0f, 2.0f );
	dig.raise = false;
	applyTerrainEdits( terrain.field, { dig } );
	CHECK( terrain.metres( 15, 15 ) == doctest::Approx( 5.0f ).epsilon( 0.01 ) );	// опускать можно — яма
}

TEST_CASE( "TerrainHydrology: catchment grows downstream along the valley floor" )
{
	TestField terrain = valley( 128, 4.0f );
	const std::vector<float> area = TerrainHydrology::catchment( terrain.field );
	REQUIRE( area.size() == terrain.heights.size() );
	const uint32_t axis = 64;
	const float upstream = area[static_cast<size_t>( 10 ) * 128 + axis];
	const float downstream = area[static_cast<size_t>( 120 ) * 128 + axis];
	const float slope = area[static_cast<size_t>( 120 ) * 128 + 10];
	CHECK( downstream > upstream );
	CHECK( downstream > 10.0f * slope );		// на склоне — своя клетка да соседи выше
}

TEST_CASE( "TerrainHydrology: a stream is generated along the valley floor and carved into it" )
{
	TestField terrain = valley( 128, 4.0f );
	WaterSimulationSettings water;
	std::vector<StreamCurve> curves;
	REQUIRE( TerrainHydrology::generate( terrain.field, water, curves ) );
	REQUIRE_FALSE( curves.empty() );

	// Самая длинная кривая — по дну: x у середины, вниз по течению z убывает
	const StreamCurve* longest = &curves.front();
	for( const StreamCurve& curve : curves )
		if( curve.points.size() > longest->points.size() )
			longest = &curve;
	REQUIRE( longest->points.size() >= 2 );
	const float middle = 0.5f * 128 * 4.0f;
	for( const StreamCurvePoint& point : longest->points )
		CHECK( std::abs( point.position.x - middle ) < 16.0f );
	CHECK( longest->points.front().position.y > longest->points.back().position.y );

	TerrainHydrology::Result result;
	REQUIRE( TerrainHydrology::build( terrain.field, water, curves, result ) );
	CHECK( result.streamCount >= 1 );
	REQUIRE( result.lowering.size() == terrain.heights.size() );
	CHECK( *std::max_element( result.lowering.begin(), result.lowering.end() ) <= 0.0f );	// русло только режет
	CHECK( *std::min_element( result.lowering.begin(), result.lowering.end() ) < 0.0f );
	// У устья на дне — вода
	REQUIRE( result.staticWater.size() == terrain.heights.size() );
	bool waterAtOutlet = false;
	for( uint32_t column = 58; column <= 70; ++column )
		waterAtOutlet |= result.staticWater[static_cast<size_t>( 120 ) * 128 + column].x > -1e8f;
	CHECK( waterAtOutlet );
}
