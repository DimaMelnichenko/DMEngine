// Ветер уровня: порыв в точке (Wind::gust — тот же расчёт, что windGust в Shaders/wind_field.sh)
#include <doctest/doctest.h>
#include <algorithm>
#include "Wind\Wind.h"

using namespace GS;

namespace
{

WindParameters breeze()
{
	WindParameters wind;
	wind.direction = DirectX::XMFLOAT2( 1.0f, 0.0f );
	wind.strength = 0.35f;
	wind.speed = 4.0f;
	wind.gustMin = 0.3f;
	wind.gustMax = 1.0f;
	wind.gustSize = 25.0f;
	return wind;
}

}

TEST_CASE( "Wind: a gust stays between the min and max gust amounts" )
{
	const WindParameters wind = breeze();
	float lowest = 10.0f, highest = -10.0f;
	for( int i = 0; i < 2000; ++i )
	{
		const float gust = Wind::gust( wind, 3.7f * i, 1.3f * ( i % 97 ), 0.11f * i );
		lowest = std::min( lowest, gust );
		highest = std::max( highest, gust );
	}
	CHECK( lowest >= wind.gustMin - 1e-4f );
	CHECK( highest <= wind.gustMax + 1e-4f );
	CHECK( highest - lowest > 0.5f * ( wind.gustMax - wind.gustMin ) );	// волны есть, а не ровный ветер
}

TEST_CASE( "Wind: gust waves run downwind with the wind speed" )
{
	const WindParameters wind = breeze();
	for( float t = 0.0f; t < 10.0f; t += 1.7f )
	{
		// Тот же порыв через dt — на speed · dt дальше по ветру
		const float dt = 2.5f;
		CHECK( Wind::gust( wind, 10.0f + wind.speed * dt, 7.0f, t + dt ) == doctest::Approx( Wind::gust( wind, 10.0f, 7.0f, t ) ).epsilon( 1e-4 ) );
	}
}
