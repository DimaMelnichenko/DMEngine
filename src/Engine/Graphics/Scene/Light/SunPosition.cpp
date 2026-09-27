#include "SunPosition.h"
#include <algorithm>
#include <cmath>

namespace
{

constexpr double pi = 3.14159265358979323846;

double radians( double degrees )
{
	return degrees * pi / 180.0;
}

double degrees( double radians )
{
	return radians * 180.0 / pi;
}

int32_t daysInMonth( int32_t year, int32_t month )
{
	static const int32_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	const bool leap = ( year % 4 == 0 && year % 100 != 0 ) || year % 400 == 0;
	return month == 2 && leap ? 29 : days[month - 1];
}

// Юлианский день начала даты (полночь UTC), григорианский календарь (Meeus, гл. 7)
double julianDay( int32_t year, int32_t month, int32_t day )
{
	if( month <= 2 )
	{
		year -= 1;
		month += 12;
	}
	const int32_t a = year / 100;
	const int32_t b = 2 - a + a / 4;
	return std::floor( 365.25 * ( year + 4716 ) ) + std::floor( 30.6001 * ( month + 1 ) ) + day + b - 1524.5;
}

template<class TYPE>
void addControl( PropertyContainer& properties, const char* name, const TYPE& value, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setControlType( GUIControlType::SLIDER );
	property->setLow( low );
	property->setHigh( high );
}

}

SunPosition::SunPosition( const Settings& settings ) :
	m_properties( "Sun position" ),
	m_year( settings.year )
{
	addControl( m_properties, "Latitude", settings.latitude, -90.0f, 90.0f );
	addControl( m_properties, "Longitude", settings.longitude, -180.0f, 180.0f );
	addControl( m_properties, "Time zone", settings.timeZone, -12.0f, 14.0f );
	addControl( m_properties, "North offset", settings.northOffset, -180.0f, 180.0f );
	addControl( m_properties, "Month", settings.month, 1.0f, 12.0f );
	addControl( m_properties, "Day", settings.day, 1.0f, 31.0f );
	addControl( m_properties, "Time of day (h)", settings.timeOfDay, 0.0f, 24.0f );
}

SunPosition::Settings SunPosition::settings() const
{
	Settings settings;
	settings.latitude = m_properties["Latitude"].data<float>();
	settings.longitude = m_properties["Longitude"].data<float>();
	settings.timeZone = m_properties["Time zone"].data<float>();
	settings.northOffset = m_properties["North offset"].data<float>();
	settings.year = m_year;
	settings.month = m_properties["Month"].data<int32_t>();
	settings.day = m_properties["Day"].data<int32_t>();
	settings.timeOfDay = m_properties["Time of day (h)"].data<float>();
	return settings;
}

SunPosition::Angles SunPosition::angles() const
{
	return compute( settings() );
}

XMFLOAT3 SunPosition::toSun() const
{
	const Angles sun = angles();
	const float elevation = XMConvertToRadians( sun.elevation );
	const float yaw = XMConvertToRadians( sun.azimuth + m_properties["North offset"].data<float>() );
	return XMFLOAT3( std::cos( elevation ) * std::sin( yaw ), std::sin( elevation ), std::cos( elevation ) * std::cos( yaw ) );
}

PropertyContainer* SunPosition::properties()
{
	return &m_properties;
}

SunPosition::Angles SunPosition::compute( const Settings& settings )
{
	// В double: юлианский день ~2,5 млн, во float от суток осталась бы четверть
	const int32_t month = std::clamp( settings.month, 1, 12 );
	const int32_t day = std::clamp( settings.day, 1, daysInMonth( settings.year, month ) );
	const double hours = settings.timeOfDay;
	const double jd = julianDay( settings.year, month, day ) + ( hours - settings.timeZone ) / 24.0;
	const double t = ( jd - 2451545.0 ) / 36525.0;	// юлианские столетия от J2000.0

	// Орбита Земли: средняя долгота и аномалия солнца, эксцентриситет
	const double meanLongitude = std::fmod( 280.46646 + t * ( 36000.76983 + t * 0.0003032 ), 360.0 );
	const double meanAnomaly = radians( 357.52911 + t * ( 35999.05029 - 0.0001537 * t ) );
	const double eccentricity = 0.016708634 - t * ( 0.000042037 + 0.0000001267 * t );

	// Видимая долгота солнца: уравнение центра, нутация и аберрация
	const double center = std::sin( meanAnomaly ) * ( 1.914602 - t * ( 0.004817 + 0.000014 * t ) ) +
						  std::sin( 2.0 * meanAnomaly ) * ( 0.019993 - 0.000101 * t ) + std::sin( 3.0 * meanAnomaly ) * 0.000289;
	const double omega = radians( 125.04 - 1934.136 * t );
	const double apparentLongitude = radians( meanLongitude + center - 0.00569 - 0.00478 * std::sin( omega ) );

	// Наклон эклиптики и склонение солнца
	const double meanObliquity = 23.0 + ( 26.0 + ( 21.448 - t * ( 46.815 + t * ( 0.00059 - t * 0.001813 ) ) ) / 60.0 ) / 60.0;
	const double obliquity = radians( meanObliquity + 0.00256 * std::cos( omega ) );
	const double declination = std::asin( std::sin( obliquity ) * std::sin( apparentLongitude ) );

	// Уравнение времени, минуты: насколько истинное солнечное время впереди среднего
	const double y = std::pow( std::tan( obliquity / 2.0 ), 2.0 );
	const double l0 = radians( meanLongitude );
	const double equationOfTime = 4.0 * degrees( y * std::sin( 2.0 * l0 ) - 2.0 * eccentricity * std::sin( meanAnomaly ) +
												 4.0 * eccentricity * y * std::sin( meanAnomaly ) * std::cos( 2.0 * l0 ) -
												 0.5 * y * y * std::sin( 4.0 * l0 ) -
												 1.25 * eccentricity * eccentricity * std::sin( 2.0 * meanAnomaly ) );

	// Истинное солнечное время места и часовой угол: 0 — солнце на юге (полдень), к западу — положительный
	const double solarMinutes = std::fmod( hours * 60.0 + equationOfTime + 4.0 * settings.longitude - 60.0 * settings.timeZone, 1440.0 );
	const double hourAngle = radians( ( solarMinutes < 0.0 ? solarMinutes + 1440.0 : solarMinutes ) / 4.0 - 180.0 );

	const double latitude = radians( settings.latitude );
	const double sinElevation = std::sin( latitude ) * std::sin( declination ) +
								std::cos( latitude ) * std::cos( declination ) * std::cos( hourAngle );
	const double azimuthFromSouth = std::atan2( std::sin( hourAngle ),
												std::cos( hourAngle ) * std::sin( latitude ) - std::tan( declination ) * std::cos( latitude ) );

	Angles angles;
	angles.elevation = static_cast<float>( degrees( std::asin( std::clamp( sinElevation, -1.0, 1.0 ) ) ) );
	angles.azimuth = static_cast<float>( std::fmod( degrees( azimuthFromSouth ) + 180.0, 360.0 ) );
	return angles;
}
