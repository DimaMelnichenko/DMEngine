#include <doctest/doctest.h>
#include <algorithm>
#include <vector>
#include "Engine\Graphics\Scene\Terrain\TerrainCanopy.h"

using namespace GS;

TEST_CASE( "TerrainCanopy: full under the crown, smooth to the edge, nothing outside" )
{
	CHECK( TerrainCanopy::cover( 0.0f, 4.0f ) == doctest::Approx( 1.0f ) );
	CHECK( TerrainCanopy::cover( 4.0f * TerrainCanopy::innerShare, 4.0f ) == doctest::Approx( 1.0f ) );
	CHECK( TerrainCanopy::cover( 3.2f, 4.0f ) == doctest::Approx( 0.5f ) );	// середина края 0,6…1
	CHECK( TerrainCanopy::cover( 3.0f, 4.0f ) > TerrainCanopy::cover( 3.6f, 4.0f ) );
	CHECK( TerrainCanopy::cover( 4.0f, 4.0f ) == doctest::Approx( 0.0f ) );
	CHECK( TerrainCanopy::cover( 1.0f, 0.0f ) == doctest::Approx( 0.0f ) );

	CHECK( TerrainCanopy::mapSize( 1024.0f ) == 2048u );
	CHECK( TerrainCanopy::mapSize( 8192.0f ) == TerrainCanopy::maxSize );
}

TEST_CASE( "TerrainCanopy: map rows go from the far edge, crowns overlap by maximum, the map edge clips" )
{
	const float world = 64.0f;
	const uint32_t size = TerrainCanopy::mapSize( world );
	REQUIRE( size == 128u );
	// Крона у дальнего края по z и ещё одна, наполовину за краем карты
	const std::vector<CanopyStamp> stamps = { { 10.0f, 60.0f, 3.0f }, { 12.0f, 60.0f, 3.0f }, { -1.0f, 30.0f, 4.0f } };
	const std::vector<uint8_t> map = TerrainCanopy::build( stamps, size, world );
	REQUIRE( map.size() == static_cast<size_t>( size ) * size );

	CHECK( TerrainCanopy::sample( map, size, world, 10.0f, 60.0f ) == doctest::Approx( 1.0f ) );
	CHECK( TerrainCanopy::sample( map, size, world, 11.0f, 60.0f ) == doctest::Approx( 1.0f ) );	// между кронами — наибольшее
	CHECK( TerrainCanopy::sample( map, size, world, 20.0f, 60.0f ) == doctest::Approx( 0.0f ) );
	CHECK( TerrainCanopy::sample( map, size, world, 10.0f, 50.0f ) == doctest::Approx( 0.0f ) );
	// z = 60 у дальнего края — верхние строки: строка (64 − 60) / 0,5 = 8
	CHECK( map[8 * size + 20] == 255 );
	CHECK( map[( size - 8 ) * size + 20] == 0 );
	// Крона за краем — её часть на карте, без выхода за массив
	CHECK( TerrainCanopy::sample( map, size, world, 0.5f, 30.0f ) == doctest::Approx( 1.0f ) );
	CHECK( TerrainCanopy::sample( map, size, world, -0.5f, 30.0f ) == doctest::Approx( 0.0f ) );
	// Лиственные — второй канал: под лиственной кроной G = R, под хвойной G = 0
	std::vector<CanopyStamp> mixed = stamps;
	mixed[1].broadleaf = true;
	const std::vector<uint8_t> two = TerrainCanopy::buildWithBroadleaf( mixed, size, world );
	REQUIRE( two.size() == 2 * map.size() );
	const size_t underBroadleaf = 8 * size + 24;	// z = 60, x = 12 — крона 1 (лиственная), крона 0 — в 4 м
	const size_t underConifer = static_cast<size_t>( 68 ) * size + 1;	// x ≈ 0,75, z = 30 — крона 2 (хвойная)
	CHECK( two[2 * underBroadleaf] == map[underBroadleaf] );
	CHECK( two[2 * underBroadleaf + 1] == map[underBroadleaf] );
	CHECK( two[2 * underConifer] == map[underConifer] );
	CHECK( two[2 * underConifer + 1] == 0 );
	CHECK( map[underConifer] > 0 );
	// Без кругов — пусто
	const std::vector<uint8_t> empty = TerrainCanopy::build( {}, size, world );
	CHECK( std::all_of( empty.begin(), empty.end(), []( uint8_t v ) { return v == 0; } ) );
}
