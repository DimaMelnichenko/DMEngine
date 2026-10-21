// Мелкая вода на сетке высот (ShallowWater): озеро на неровном дне стоит, объём сохраняется, глубина не уходит ниже нуля;
// прорыв плотины — уровень между волнами по решению Стокера, фронт по сухому дну — по Риттеру; ручей на уклоне — нормальная
// глубина по Маннингу; гидравлический прыжок — сопряжённые глубины по Беланже, прыжок стоит на месте
#include <doctest/doctest.h>
#include <cmath>
#include "Water\ShallowWater.h"

using namespace GS;

namespace
{

constexpr float g = 9.81f;

// Канал вдоль X шириной в три ячейки (стенки сверху и снизу)
ShallowWater channel( uint32_t length, float cell, float manning = 0.0f )
{
	ShallowWater::Settings settings;
	settings.cell = cell;
	settings.manning = manning;
	return ShallowWater( length, 3, settings );
}

// Средняя глубина по ширине канала в столбце x
float columnDepth( const ShallowWater& water, uint32_t x )
{
	float sum = 0.0f;
	for( uint32_t z = 0; z < water.height(); ++z )
		sum += water.depth( x, z );
	return sum / water.height();
}

void run( ShallowWater& water, float seconds, float frame = 1.0f / 60.0f )
{
	for( float t = 0.0f; t < seconds - 1e-6f; t += frame )
		water.step( frame );
}

}

TEST_CASE( "ShallowWater: a lake at rest on an uneven bed with islands stays at rest" )
{
	ShallowWater::Settings settings;
	settings.cell = 0.1f;
	settings.manning = 0.03f;
	ShallowWater water( 40, 40, settings );
	const float level = 0.5f;
	for( uint32_t z = 0; z < 40; ++z )
		for( uint32_t x = 0; x < 40; ++x )
		{
			// Холмы дна, два — выше воды (острова)
			const float bed = 0.25f + 0.2f * std::sin( 0.4f * x ) * std::cos( 0.3f * z ) +
							  0.5f * std::exp( -( ( x - 12.0f ) * ( x - 12.0f ) + ( z - 25.0f ) * ( z - 25.0f ) ) / 8.0f ) +
							  0.4f * std::exp( -( ( x - 30.0f ) * ( x - 30.0f ) + ( z - 10.0f ) * ( z - 10.0f ) ) / 5.0f );
			water.bed( x, z ) = bed;
			water.depth( x, z ) = std::max( level - bed, 0.0f );
		}
	const double before = water.volume();
	run( water, 3.0f );
	float speed = 0.0f;
	float drift = 0.0f;
	for( uint32_t z = 0; z < 40; ++z )
		for( uint32_t x = 0; x <= 40; ++x )
			speed = std::max( speed, std::fabs( water.velocityX( x, z ) ) );
	for( uint32_t z = 0; z < 40; ++z )
		for( uint32_t x = 0; x < 40; ++x )
		{
			speed = std::max( speed, std::fabs( water.velocityZ( x, z ) ) );
			if( water.depth( x, z ) > 0.0f )
				drift = std::max( drift, std::fabs( water.surface( x, z ) - level ) );
		}
	CHECK( speed < 1e-5f );
	CHECK( drift < 1e-5f );
	CHECK( water.volume() == doctest::Approx( before ).epsilon( 1e-6 ) );
}

TEST_CASE( "ShallowWater: a dam break keeps the volume and no depth goes below zero" )
{
	ShallowWater::Settings settings;
	settings.cell = 0.05f;
	ShallowWater water( 60, 60, settings );
	for( uint32_t z = 0; z < 60; ++z )
		for( uint32_t x = 0; x < 60; ++x )
		{
			water.bed( x, z ) = 0.02f * std::sin( 0.5f * x + 0.3f * z );
			water.depth( x, z ) = x < 20 && z < 30 ? 0.4f : 0.0f;
		}
	const double before = water.volume();
	float lowest = 0.0f;
	for( int i = 0; i < 120; ++i )
	{
		water.step( 1.0f / 60.0f );
		for( uint32_t z = 0; z < 60; ++z )
			for( uint32_t x = 0; x < 60; ++x )
				lowest = std::min( lowest, water.depth( x, z ) );
	}
	CHECK( lowest >= 0.0f );
	CHECK( water.volume() == doctest::Approx( before ).epsilon( 1e-4 ) );
}

TEST_CASE( "ShallowWater: a wet dam break gives the middle depth and the shock of the Stoker solution" )
{
	// Канал 20 м, слева 1 м воды, справа 0,5 м; через 1 с между волной разрежения и прыжком — глубина hm
	const float cell = 0.04f;
	const uint32_t length = 500;
	ShallowWater water = channel( length, cell );
	const float hL = 1.0f, hR = 0.5f;
	for( uint32_t z = 0; z < 3; ++z )
		for( uint32_t x = 0; x < length; ++x )
			water.depth( x, z ) = x < length / 2 ? hL : hR;
	// Решение Стокера: um = 2 (√(g hL) − √(g hm)) — то же, что скорость за прыжком (hm − hR) √(g (hm + hR) / (2 hm hR))
	float low = hR, high = hL;
	for( int i = 0; i < 60; ++i )
	{
		const float hm = 0.5f * ( low + high );
		const float rarefaction = 2.0f * ( std::sqrt( g * hL ) - std::sqrt( g * hm ) );
		const float shock = ( hm - hR ) * std::sqrt( g * ( hm + hR ) / ( 2.0f * hm * hR ) );
		( rarefaction > shock ? low : high ) = hm;
	}
	const float hm = 0.5f * ( low + high );
	const float um = 2.0f * ( std::sqrt( g * hL ) - std::sqrt( g * hm ) );
	const float shockSpeed = hm * um / ( hm - hR );
	const float tailSpeed = um - std::sqrt( g * hm );
	const float t = 1.0f;
	run( water, t );
	// Середина плато между хвостом разрежения и прыжком
	const float middle = length / 2 * cell + 0.5f * ( tailSpeed + shockSpeed ) * t;
	const uint32_t at = static_cast<uint32_t>( middle / cell );
	CHECK( columnDepth( water, at ) == doctest::Approx( hm ).epsilon( 0.03 ) );
	// Прыжок — где глубина переходит середину между hm и hR
	float shockAt = 0.0f;
	for( uint32_t x = at; x < length; ++x )
		if( columnDepth( water, x ) < 0.5f * ( hm + hR ) )
		{
			shockAt = x * cell;
			break;
		}
	CHECK( shockAt == doctest::Approx( length / 2 * cell + shockSpeed * t ).epsilon( 0.03 ) );
}

TEST_CASE( "ShallowWater: water spreads over a dry bed and the front moves no faster than Ritter's 2 sqrt( g h )" )
{
	const float cell = 0.04f;
	const uint32_t length = 400;
	ShallowWater water = channel( length, cell );
	const float h0 = 0.3f;
	for( uint32_t z = 0; z < 3; ++z )
		for( uint32_t x = 0; x < length / 4; ++x )
			water.depth( x, z ) = h0;
	const float t = 0.5f;
	run( water, t );
	float front = 0.0f;
	for( uint32_t x = 0; x < length; ++x )
		if( columnDepth( water, x ) > 1e-3f )
			front = ( x + 1 ) * cell - length / 4 * cell;
	const float ritter = 2.0f * std::sqrt( g * h0 ) * t;
	CHECK( front > 0.6f * ritter );
	CHECK( front < 1.05f * ritter );
}

TEST_CASE( "ShallowWater: a stream on a slope settles to the normal depth of Manning" )
{
	// Уклон 1 %, n 0,05, расход 0,05 м²/с: нормальная глубина (q n / √S)^0,6
	const float cell = 0.1f;
	const uint32_t length = 200;
	const float slope = 0.01f, manning = 0.05f, q = 0.05f;
	ShallowWater water = channel( length, cell, manning );
	const float normal = std::pow( q * manning / std::sqrt( slope ), 0.6f );
	for( uint32_t z = 0; z < 3; ++z )
	{
		for( uint32_t x = 0; x < length; ++x )
		{
			water.bed( x, z ) = slope * ( length - x ) * cell;
			water.depth( x, z ) = 0.5f * normal;
		}
		// Вход: первая ячейка — глубина нормальная, грань за ней — расход; выход — обрыв в сток
		water.depth( 0, z ) = normal;
		water.setCellFixed( 0, z, true );
		water.velocityX( 1, z ) = q / normal;
		water.setFaceXFixed( 1, z, true );
		water.bed( length - 1, z ) = -1.0f;
		water.depth( length - 1, z ) = 0.0f;
		water.setCellFixed( length - 1, z, true );
	}
	run( water, 60.0f );
	for( uint32_t x : { length * 3 / 10, length / 2 } )
		CHECK( columnDepth( water, x ) == doctest::Approx( normal ).epsilon( 0.05 ) );
}

TEST_CASE( "ShallowWater: a hydraulic jump holds the conjugate depths of Belanger and stays in place" )
{
	// Бурный поток h1 = 5 см, 2 м/с (Фруд 2,9) входит в канал, на выходе — глубина за прыжком по Беланже:
	// h2 = h1 / 2 · (√(1 + 8 Fr²) − 1)
	const float cell = 0.05f;
	const uint32_t length = 300;
	const float h1 = 0.05f, u1 = 2.0f;
	const float froude = u1 / std::sqrt( g * h1 );
	const float h2 = 0.5f * h1 * ( std::sqrt( 1.0f + 8.0f * froude * froude ) - 1.0f );
	ShallowWater water = channel( length, cell );
	for( uint32_t z = 0; z < 3; ++z )
	{
		for( uint32_t x = 0; x < length; ++x )
		{
			water.depth( x, z ) = x < length / 2 ? h1 : h2;
			water.velocityX( x + 1, z ) = x < length / 2 ? u1 : h1 * u1 / h2;
		}
		water.setCellFixed( 0, z, true );
		water.setFaceXFixed( 1, z, true );
		water.depth( length - 1, z ) = h2;
		water.setCellFixed( length - 1, z, true );
		// Выход: за последней ячейкой — тот же расход, что на входе (вода уходит с глубиной h2)
		water.velocityX( length, z ) = h1 * u1 / h2;
		water.setFaceXFixed( length, z, true );
	}
	auto jumpAt = [&]()
	{
		for( uint32_t x = 0; x < length; ++x )
			if( columnDepth( water, x ) > 0.5f * ( h1 + h2 ) )
				return x * cell;
		return length * cell;
	};
	run( water, 10.0f );
	const float early = jumpAt();
	run( water, 10.0f );
	const float late = jumpAt();
	MESSAGE( "jump at " << early << " m, then " << late << " m (h1 " << h1 << ", h2 " << h2 << ")" );
	CHECK( columnDepth( water, length / 10 ) == doctest::Approx( h1 ).epsilon( 0.1 ) );
	CHECK( columnDepth( water, length * 9 / 10 ) == doctest::Approx( h2 ).epsilon( 0.05 ) );
	CHECK( late > 0.1f * length * cell );
	CHECK( late < 0.9f * length * cell );
	CHECK( std::fabs( late - early ) < 0.5f );
}
