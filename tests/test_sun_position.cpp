// Время суток (SunPosition): высота и азимут солнца по формулам NOAA, фаза луны, ход времени через полночь и Новый год
#include <doctest/doctest.h>
#include "Light\SunPosition.h"

namespace
{

SunPosition::Settings place( float latitude, int32_t year, int32_t month, int32_t day, float hours )
{
	SunPosition::Settings settings;
	settings.latitude = latitude;
	settings.longitude = 0.0f;
	settings.timeZone = 0.0f;
	settings.northOffset = 0.0f;
	settings.year = year;
	settings.month = month;
	settings.day = day;
	settings.timeOfDay = hours;
	return settings;
}

// Солнце в полдень дня: наибольшая высота за 11…13 ч по местным часам (на долготе 0 полдень гуляет на минуты — уравнение
// времени) и его азимут
SunPosition::Angles noon( float latitude, int32_t month, int32_t day )
{
	SunPosition::Angles best;
	best.elevation = -90.0f;
	for( float hours = 11.0f; hours <= 13.0f; hours += 0.01f )
	{
		const SunPosition::Angles angles = SunPosition::compute( place( latitude, 2026, month, day, hours ) );
		if( angles.elevation > best.elevation )
			best = angles;
	}
	return best;
}

}

TEST_CASE( "SunPosition: noon elevation is 90 - latitude + declination" )
{
	// Солнцестояния: склонение ±23,44°
	CHECK( noon( 45.0f, 6, 21 ).elevation == doctest::Approx( 90.0f - 45.0f + 23.44f ).epsilon( 0.01 ) );
	CHECK( noon( 60.0f, 12, 21 ).elevation == doctest::Approx( 90.0f - 60.0f - 23.44f ).epsilon( 0.05 ) );
	// Равноденствие на экваторе — почти в зените
	CHECK( noon( 0.0f, 3, 20 ).elevation > 88.0f );
}

TEST_CASE( "SunPosition: noon sun is south in the northern hemisphere, morning sun is east" )
{
	CHECK( noon( 45.0f, 6, 21 ).azimuth == doctest::Approx( 180.0f ).epsilon( 0.01 ) );
	const SunPosition::Angles morning = SunPosition::compute( place( 45.0f, 2026, 6, 21, 8.0f ) );
	CHECK( morning.azimuth > 60.0f );
	CHECK( morning.azimuth < 120.0f );
	CHECK( morning.elevation > 0.0f );
	CHECK( morning.elevation < noon( 45.0f, 6, 21 ).elevation );
	CHECK( SunPosition::compute( place( 45.0f, 2026, 6, 21, 0.0f ) ).elevation < 0.0f );
}

TEST_CASE( "SunPosition: moon phase at a known full and new moon" )
{
	// Полнолуние 2024-01-25 17:54 UTC, новолуние 2024-01-11 11:57 UTC
	CHECK( SunPosition::computeMoon( place( 45.0f, 2024, 1, 25, 17.9f ) ).illuminatedFraction > 0.98f );
	CHECK( SunPosition::computeMoon( place( 45.0f, 2024, 1, 11, 11.95f ) ).illuminatedFraction < 0.02f );
}

TEST_CASE( "SunPosition: time advances past midnight into the next day, month and year" )
{
	SunPosition::Settings settings = place( 45.0f, 2026, 6, 30, 23.5f );
	settings.timeScale = 3600.0f;	// час за секунду
	SunPosition june( settings );
	june.advance( 1.0f );
	SunPosition::Settings next = june.settings();
	CHECK( next.month == 7 );
	CHECK( next.day == 1 );
	CHECK( next.timeOfDay == doctest::Approx( 0.5f ).epsilon( 0.001 ) );

	SunPosition::Settings december = place( 45.0f, 2026, 12, 31, 23.0f );
	december.timeScale = 3600.0f;
	SunPosition newYear( december );
	newYear.advance( 2.0f );
	next = newYear.settings();
	CHECK( next.year == 2027 );
	CHECK( next.month == 1 );
	CHECK( next.day == 1 );

	// Скорость 0 — время стоит
	SunPosition stopped( place( 45.0f, 2026, 6, 21, 12.0f ) );
	stopped.advance( 10.0f );
	CHECK( stopped.settings().timeOfDay == doctest::Approx( 12.0f ) );
}
