// Узор пены «кружево» (generateFoamLace): бесшовен, маска и шум растворения выровнены по гистограмме (порог по доле пены
// оставляет ту же долю площади), пены мало — остаются тонкие стенки, а не пятна, одинаков при том же зерне
#include <doctest/doctest.h>
#include <cmath>
#include "Water\FoamLace.h"

using namespace GS;
using namespace DirectX;

namespace
{

constexpr uint32_t size = 64;

float mask( const std::vector<XMFLOAT4>& texels, uint32_t x, uint32_t y )
{
	return texels[static_cast<size_t>( y ) * size + x].x;
}

}

TEST_CASE( "FoamLace: the pattern is seamless across the edges" )
{
	const std::vector<XMFLOAT4> texels = generateFoamLace( size, 1 );
	REQUIRE( texels.size() == size * size );
	// Скачок через край — как между соседними текселями внутри
	double inside = 0.0, across = 0.0;
	for( uint32_t y = 0; y < size; ++y )
	{
		across += std::fabs( mask( texels, size - 1, y ) - mask( texels, 0, y ) ) + std::fabs( mask( texels, y, size - 1 ) - mask( texels, y, 0 ) );
		inside += std::fabs( mask( texels, size / 2 - 1, y ) - mask( texels, size / 2, y ) ) +
				  std::fabs( mask( texels, y, size / 2 - 1 ) - mask( texels, y, size / 2 ) );
	}
	CHECK( across < 1.6 * inside );
}

TEST_CASE( "FoamLace: the mask and the dissolve noise are uniform, a foam share passes the same share of the area" )
{
	const std::vector<XMFLOAT4> texels = generateFoamLace( size, 1 );
	double maskSum = 0.0, dissolveSum = 0.0;
	size_t above = 0;
	for( const XMFLOAT4& texel : texels )
	{
		CHECK( texel.x >= 0.0f );
		CHECK( texel.x <= 1.0f );
		maskSum += texel.x;
		dissolveSum += texel.w;
		above += texel.x > 0.8f ? 1 : 0;
		// Нормаль — единичная, вверх
		const float nx = 2.0f * texel.y - 1.0f;
		const float ny = 2.0f * texel.z - 1.0f;
		CHECK( nx * nx + ny * ny < 1.0f );
	}
	CHECK( maskSum / texels.size() == doctest::Approx( 0.5 ).epsilon( 0.01 ) );
	CHECK( dissolveSum / texels.size() == doctest::Approx( 0.5 ).epsilon( 0.01 ) );
	CHECK( static_cast<double>( above ) / texels.size() == doctest::Approx( 0.2 ).epsilon( 0.02 ) );
}

TEST_CASE( "FoamLace: with little foam thin walls remain, not blobs" )
{
	// Пены мало (порог оставляет 15 % площади): видимое — тонкие стенки, почти каждый их тексель — у края. Прежде маска была
	// суммой стенок и плотности, и от кружева оставались узлы и пятна — у края 74–77 % текселей (стенки по расстоянию до
	// середины — 88–93 %)
	constexpr uint32_t side = 256;
	for( uint32_t seed : { 1u, 2u, 3u } )
	{
		const std::vector<XMFLOAT4> texels = generateFoamLace( side, seed );
		size_t visible = 0, edge = 0;
		for( uint32_t y = 0; y < side; ++y )
			for( uint32_t x = 0; x < side; ++x )
			{
				const float threshold = 0.85f;
				if( texels[static_cast<size_t>( y ) * side + x].x < threshold )
					continue;
				++visible;
				const uint32_t xs[4] = { ( x + 1 ) % side, ( x + side - 1 ) % side, x, x };
				const uint32_t ys[4] = { y, y, ( y + 1 ) % side, ( y + side - 1 ) % side };
				bool border = false;
				for( int k = 0; k < 4; ++k )
					border = border || texels[static_cast<size_t>( ys[k] ) * side + xs[k]].x < threshold;
				edge += border ? 1 : 0;
			}
		CHECK( static_cast<double>( edge ) / visible > 0.85 );
	}
}

TEST_CASE( "FoamLace: dense foam is seamless, its gaps are thin foam, not water" )
{
	const std::vector<XMFLOAT4> texels = generateFoamDense( size, 1 );
	REQUIRE( texels.size() == size * size );
	double inside = 0.0, across = 0.0, sum = 0.0;
	for( uint32_t y = 0; y < size; ++y )
	{
		across += std::fabs( mask( texels, size - 1, y ) - mask( texels, 0, y ) ) + std::fabs( mask( texels, y, size - 1 ) - mask( texels, y, 0 ) );
		inside += std::fabs( mask( texels, size / 2 - 1, y ) - mask( texels, size / 2, y ) ) +
				  std::fabs( mask( texels, y, size / 2 - 1 ) - mask( texels, y, size / 2 ) );
	}
	for( const XMFLOAT4& texel : texels )
	{
		CHECK( texel.x >= denseFoamGap );
		CHECK( texel.x <= 1.0f );
		sum += texel.x;
	}
	CHECK( across < 1.6 * inside );
	CHECK( sum / texels.size() == doctest::Approx( 0.5 * ( 1.0 + denseFoamGap ) ).epsilon( 0.01 ) );
}

TEST_CASE( "FoamLace: the same seed gives the same pattern, another seed another one" )
{
	const std::vector<XMFLOAT4> a = generateFoamLace( size, 3 );
	const std::vector<XMFLOAT4> b = generateFoamLace( size, 3 );
	const std::vector<XMFLOAT4> c = generateFoamLace( size, 4 );
	size_t same = 0, other = 0;
	for( size_t i = 0; i < a.size(); ++i )
	{
		same += a[i].x == b[i].x && a[i].w == b[i].w ? 1 : 0;
		other += std::fabs( a[i].x - c[i].x ) > 0.1f ? 1 : 0;
	}
	CHECK( same == a.size() );
	CHECK( other > a.size() / 4 );
}
