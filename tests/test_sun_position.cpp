// Время суток (SunPosition): высота и азимут солнца по формулам NOAA, фаза луны, окно «Sun position» — органы мировых часов
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

TEST_CASE( "SunPosition: the window drives the world clock and shows it" )
{
	GS::WorldClock clock;
	SunPosition sun( place( 45.0f, 2026, 6, 21, 6.0f ), clock );
	CHECK( clock.hours() == doctest::Approx( 6.0 ) );
	CHECK( clock.date().day == 21 );

	// Правка окна — в часы до их шага
	PropertyContainer& window = *sun.properties();
	window["Time of day"].setData( 12.0f );
	window["Time scale"].setData( 3600.0f );	// час за секунду
	sun.takeWindowEdits();
	CHECK( clock.hours() == doctest::Approx( 12.0 ) );
	CHECK( clock.timeScale() == doctest::Approx( 3600.0f ) );

	// Шаг часов — в окно, в дату и в высоту солнца: 13 часов спустя — час ночи следующего дня
	clock.advance( 13.0 );
	sun.showClock();
	CHECK( window["Time of day"].data<float>() == doctest::Approx( 1.0f ) );
	CHECK( window["Day"].data<int32_t>() == 22 );
	CHECK( sun.settings().day == 22 );
	CHECK( sun.angles().elevation < 0.0f );

	// Пауза окна — пауза часов
	window["Time paused"].setData( true );
	sun.takeWindowEdits();
	CHECK( clock.paused() );
	clock.advance( 5.0 );
	CHECK( clock.hours() == doctest::Approx( 1.0 ) );
}
