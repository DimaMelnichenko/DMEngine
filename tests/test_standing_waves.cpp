// Волновое число стоячей волны (standingWavenumber): на глубокой воде — g / v², на мелкой медленнее; не длиннее 2π глубины
// и не короче 10 см; скорость волны с этим числом по дисперсии равна скорости воды
#include <doctest/doctest.h>
#include <cmath>
#include "Water\StandingWaves.h"

using namespace GS;

TEST_CASE( "StandingWaves: deep water gives g / v^2, the wave speed equals the flow speed" )
{
	// Глубоко (h = 5 м), 1 м/с: k ≈ 9,81 / 1 — волна 0,64 м
	const float k = standingWavenumber( 1.0f, 5.0f );
	CHECK( k == doctest::Approx( standingGravity ).epsilon( 0.01 ) );
	// На перекате (h = 0,2 м, 1,2 м/с): фазовая скорость √(g th(kh) / k) — та же, что у воды
	const float shallow = standingWavenumber( 1.2f, 0.2f );
	CHECK( std::sqrt( standingGravity * std::tanh( shallow * 0.2f ) / shallow ) == doctest::Approx( 1.2f ).epsilon( 0.03 ) );
	// Мельче — длиннее, чем на глубокой воде при той же скорости
	CHECK( shallow < standingWavenumber( 1.2f, 5.0f ) );
}

TEST_CASE( "StandingWaves: the wave is not longer than 2 pi depth and not shorter than 10 cm" )
{
	// Почти критическое течение (Фруд ~ 1): k h не меньше 1
	CHECK( standingWavenumber( 1.4f, 0.2f ) * 0.2f >= 1.0f - 1e-4f );
	// Медленная вода — короткие волны, но не короче 10 см
	CHECK( standingWavenumber( 0.05f, 1.0f ) == doctest::Approx( 6.2831853f / standingMinWavelength ) );
}
