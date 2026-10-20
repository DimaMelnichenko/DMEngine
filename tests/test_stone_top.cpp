// Верх камня сверху (rasterizeStoneTop): квадрат по размаху меша в мире, в текселе — наибольшая высота поверхности над
// низом камня по центру текселя; вне меша — noTop; поворот и масштаб — мировой матрицей
#include <doctest/doctest.h>
#include <cmath>
#include "Water\StoneTop.h"

using namespace GS;
using namespace DirectX;

namespace
{

// Куб [-0,5; 0,5]³: 8 вершин, 12 треугольников
void cube( std::vector<XMFLOAT3>& points, std::vector<uint32_t>& indices )
{
	points.clear();
	for( int k = 0; k < 8; ++k )
		points.push_back( XMFLOAT3( k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, k & 4 ? 0.5f : -0.5f ) );
	indices = { 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3 };
}

}

TEST_CASE( "StoneTop: a scaled and moved cube is flat on top over the whole square" )
{
	std::vector<XMFLOAT3> points;
	std::vector<uint32_t> indices;
	cube( points, indices );
	const StoneTop top = rasterizeStoneTop( points, indices, XMMatrixScaling( 2.0f, 2.0f, 2.0f ) * XMMatrixTranslation( 10.0f, 5.0f, 20.0f ) );
	CHECK( top.side == doctest::Approx( 2.0f ) );
	CHECK( top.origin.x == doctest::Approx( 9.0f ) );
	CHECK( top.origin.y == doctest::Approx( 19.0f ) );
	CHECK( top.base == doctest::Approx( 4.0f ) );
	REQUIRE( top.heights.size() == StoneTop::cells * StoneTop::cells );
	for( float height : top.heights )
		CHECK( height == doctest::Approx( 2.0f ) );
}

TEST_CASE( "StoneTop: a cube turned by 45 degrees leaves the corners of the square empty" )
{
	std::vector<XMFLOAT3> points;
	std::vector<uint32_t> indices;
	cube( points, indices );
	const StoneTop top = rasterizeStoneTop( points, indices, XMMatrixRotationY( XM_PIDIV4 ) );
	CHECK( top.side == doctest::Approx( std::sqrt( 2.0f ) ) );
	const uint32_t n = StoneTop::cells;
	CHECK( top.heights[0] == StoneTop::noTop );
	CHECK( top.heights[n - 1] == StoneTop::noTop );
	CHECK( top.heights[( n - 1 ) * n] == StoneTop::noTop );
	CHECK( top.heights[n * n - 1] == StoneTop::noTop );
	CHECK( top.heights[( n / 2 ) * n + n / 2] == doctest::Approx( 1.0f ) );
}

TEST_CASE( "StoneTop: a pyramid rises linearly to its apex" )
{
	// Основание 2 × 2 м, вершина в 1 м над серединой: высота в точке — 1 − max( |x|, |z| )
	const std::vector<XMFLOAT3> points = { XMFLOAT3( -1.0f, 0.0f, -1.0f ), XMFLOAT3( 1.0f, 0.0f, -1.0f ), XMFLOAT3( 1.0f, 0.0f, 1.0f ),
										   XMFLOAT3( -1.0f, 0.0f, 1.0f ), XMFLOAT3( 0.0f, 1.0f, 0.0f ) };
	const std::vector<uint32_t> indices = { 0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4, 0, 2, 1, 0, 3, 2 };
	const StoneTop top = rasterizeStoneTop( points, indices, XMMatrixIdentity() );
	const uint32_t n = StoneTop::cells;
	for( uint32_t j = 0; j < n; ++j )
		for( uint32_t i = 0; i < n; ++i )
		{
			const float x = top.origin.x + ( i + 0.5f ) * top.side / n;
			const float z = top.origin.y + ( j + 0.5f ) * top.side / n;
			CHECK( top.heights[j * n + i] == doctest::Approx( 1.0f - std::max( std::fabs( x ), std::fabs( z ) ) ).epsilon( 1e-4 ) );
		}
}

TEST_CASE( "StoneTop: an empty mesh has no square" )
{
	const StoneTop top = rasterizeStoneTop( {}, {}, XMMatrixIdentity() );
	CHECK( top.side == 0.0f );
	CHECK( top.heights.empty() );
}
