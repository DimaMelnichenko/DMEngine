// Мировые часы (WorldClock): ход времени суток со скоростью, переход через полночь в следующий день, месяц и год (с
// високосным), пауза и остановленное время игры (-freezetime)
#include <doctest/doctest.h>
#include "Engine\Simulation\WorldClock.h"

using namespace GS;

namespace
{

WorldClock clockAt( int32_t year, int32_t month, int32_t day, double hours, float timeScale )
{
	WorldClock clock;
	clock.setDate( { year, month, day } );
	clock.setHours( hours );
	clock.setTimeScale( timeScale );
	return clock;
}

void checkDate( const WorldClock& clock, int32_t year, int32_t month, int32_t day )
{
	CHECK( clock.date().year == year );
	CHECK( clock.date().month == month );
	CHECK( clock.date().day == day );
}

}

TEST_CASE( "WorldClock: hours go with the time scale, game time with real seconds" )
{
	WorldClock clock = clockAt( 2026, 6, 21, 12.0, 60.0f );	// минута за секунду
	clock.advance( 30.0 );
	CHECK( clock.hours() == doctest::Approx( 12.5 ) );
	CHECK( clock.gameSeconds() == doctest::Approx( 30.0 ) );
	checkDate( clock, 2026, 6, 21 );
}

TEST_CASE( "WorldClock: past midnight into the next day, month and year" )
{
	WorldClock june = clockAt( 2026, 6, 30, 23.5, 3600.0f );	// час за секунду
	june.advance( 1.0 );
	checkDate( june, 2026, 7, 1 );
	CHECK( june.hours() == doctest::Approx( 0.5 ) );

	WorldClock december = clockAt( 2026, 12, 31, 23.0, 3600.0f );
	december.advance( 2.0 );
	checkDate( december, 2027, 1, 1 );
	CHECK( december.hours() == doctest::Approx( 1.0 ) );

	// Високосный год: за 28 февраля — 29-е, в обычный — 1 марта
	WorldClock leap = clockAt( 2028, 2, 28, 23.0, 3600.0f );
	leap.advance( 2.0 );
	checkDate( leap, 2028, 2, 29 );
	WorldClock common = clockAt( 2026, 2, 28, 23.0, 3600.0f );
	common.advance( 2.0 );
	checkDate( common, 2026, 3, 1 );

	// Несколько суток за кадр
	WorldClock days = clockAt( 2026, 6, 21, 12.0, 3600.0f );
	days.advance( 24.0 * 3.0 );
	checkDate( days, 2026, 6, 24 );
	CHECK( days.hours() == doctest::Approx( 12.0 ) );
}

TEST_CASE( "WorldClock: pause and zero scale stop the hours, not the game time" )
{
	WorldClock paused = clockAt( 2026, 6, 21, 12.0, 3600.0f );
	paused.setPaused( true );
	paused.advance( 10.0 );
	CHECK( paused.hours() == doctest::Approx( 12.0 ) );
	CHECK( paused.gameSeconds() == doctest::Approx( 10.0 ) );

	WorldClock stopped = clockAt( 2026, 6, 21, 12.0, 0.0f );
	stopped.advance( 10.0 );
	CHECK( stopped.hours() == doctest::Approx( 12.0 ) );
}

TEST_CASE( "WorldClock: frozen game time stands, the hours still go" )
{
	WorldClock clock = clockAt( 2026, 6, 21, 12.0, 3600.0f );
	clock.setGameTimeRunning( false );
	clock.advance( 1.0 );
	CHECK( clock.gameSeconds() == 0.0 );
	CHECK( clock.hours() == doctest::Approx( 13.0 ) );
}

TEST_CASE( "WorldClock: a date out of range is clamped" )
{
	WorldClock clock;
	clock.setDate( { 2026, 4, 31 } );
	checkDate( clock, 2026, 4, 30 );
	clock.setDate( { 2026, 13, 1 } );
	checkDate( clock, 2026, 12, 1 );
	clock.setHours( 25.0 );
	CHECK( clock.hours() == doctest::Approx( 24.0 ) );
}
