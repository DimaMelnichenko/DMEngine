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

// Юлианский день момента настроек (UT): дата и местное время с часовым поясом
double julianDate( const SunPosition::Settings& settings )
{
	const int32_t month = std::clamp( settings.month, 1, 12 );
	const int32_t day = std::clamp( settings.day, 1, daysInMonth( settings.year, month ) );
	return julianDay( settings.year, month, day ) + ( settings.timeOfDay - settings.timeZone ) / 24.0;
}

// Видимая долгота солнца и наклон эклиптики (NOAA по Meeus) — общее для солнца и фазы луны; t — юлианские столетия
void solarCoordinates( double t, double& apparentLongitude, double& obliquity, double& meanLongitude, double& meanAnomaly )
{
	meanLongitude = std::fmod( 280.46646 + t * ( 36000.76983 + t * 0.0003032 ), 360.0 );
	meanAnomaly = radians( 357.52911 + t * ( 35999.05029 - 0.0001537 * t ) );
	const double center = std::sin( meanAnomaly ) * ( 1.914602 - t * ( 0.004817 + 0.000014 * t ) ) +
						  std::sin( 2.0 * meanAnomaly ) * ( 0.019993 - 0.000101 * t ) + std::sin( 3.0 * meanAnomaly ) * 0.000289;
	const double omega = radians( 125.04 - 1934.136 * t );
	apparentLongitude = radians( meanLongitude + center - 0.00569 - 0.00478 * std::sin( omega ) );
	const double meanObliquity = 23.0 + ( 26.0 + ( 21.448 - t * ( 46.815 + t * ( 0.00059 - t * 0.001813 ) ) ) / 60.0 ) / 60.0;
	obliquity = radians( meanObliquity + 0.00256 * std::cos( omega ) );
}

// Звёздное время Гринвича, радианы (Meeus, 12.4); jd — юлианский день UT
double greenwichSiderealTime( double jd )
{
	const double t = ( jd - 2451545.0 ) / 36525.0;
	const double theta = 280.46061837 + 360.98564736629 * ( jd - 2451545.0 ) + t * t * ( 0.000387933 - t / 38710000.0 );
	const double wrapped = std::fmod( theta, 360.0 );
	return radians( wrapped < 0.0 ? wrapped + 360.0 : wrapped );
}

// Высота и азимут (от севера через восток), градусы: по часовому углу (к западу > 0), склонению и широте, радианы
SunPosition::Angles horizontal( double hourAngle, double declination, double latitude )
{
	const double sinElevation = std::sin( latitude ) * std::sin( declination ) +
								std::cos( latitude ) * std::cos( declination ) * std::cos( hourAngle );
	const double azimuthFromSouth = std::atan2( std::sin( hourAngle ),
												std::cos( hourAngle ) * std::sin( latitude ) - std::tan( declination ) * std::cos( latitude ) );
	SunPosition::Angles angles;
	angles.elevation = static_cast<float>( degrees( std::asin( std::clamp( sinElevation, -1.0, 1.0 ) ) ) );
	angles.azimuth = static_cast<float>( std::fmod( degrees( azimuthFromSouth ) + 180.0, 360.0 ) );
	return angles;
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
	m_year( settings.year ),
	m_hours( settings.timeOfDay ),
	m_hoursShown( settings.timeOfDay )
{
	addControl( m_properties, "Latitude", settings.latitude, -90.0f, 90.0f );
	addControl( m_properties, "Longitude", settings.longitude, -180.0f, 180.0f );
	addControl( m_properties, "Time zone", settings.timeZone, -12.0f, 14.0f );
	addControl( m_properties, "North offset", settings.northOffset, -180.0f, 180.0f );
	addControl( m_properties, "Month", settings.month, 1.0f, 12.0f );
	addControl( m_properties, "Day", settings.day, 1.0f, 31.0f );
	addControl( m_properties, "Time of day (h)", settings.timeOfDay, 0.0f, 24.0f );
	Property* timeScale = m_properties.insert( "Time scale", settings.timeScale );
	timeScale->setControlType( GUIControlType::DRAG );
	timeScale->setLow( 0.0f );
	timeScale->setHigh( 100000.0f );
	m_properties.insert( "Time paused", false );
}

void SunPosition::advance( float seconds )
{
	const float shown = m_properties["Time of day (h)"].data<float>();
	if( shown != m_hoursShown )
		m_hours = shown;

	const float scale = m_properties["Time scale"].data<float>();
	if( scale <= 0.0f || seconds <= 0.0f || m_properties["Time paused"].data<bool>() )
		return;

	m_hours += static_cast<double>( seconds ) * scale / 3600.0;
	if( m_hours >= 24.0 )
	{
		// Через полночь — следующий день: дата двигает склонение солнца, за год — сезоны
		int32_t month = std::clamp( m_properties["Month"].data<int32_t>(), 1, 12 );
		int32_t day = std::clamp( m_properties["Day"].data<int32_t>(), 1, daysInMonth( m_year, month ) );
		while( m_hours >= 24.0 )
		{
			m_hours -= 24.0;
			if( ++day > daysInMonth( m_year, month ) )
			{
				day = 1;
				if( ++month > 12 )
				{
					month = 1;
					++m_year;
				}
			}
		}
		m_properties["Month"].setData( month );
		m_properties["Day"].setData( day );
	}
	m_hoursShown = static_cast<float>( m_hours );
	m_properties["Time of day (h)"].setData( m_hoursShown );
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
	settings.timeScale = m_properties["Time scale"].data<float>();
	return settings;
}

SunPosition::Angles SunPosition::angles() const
{
	return compute( settings() );
}

XMFLOAT3 SunPosition::direction( const Angles& angles, float northOffset )
{
	const float elevation = XMConvertToRadians( angles.elevation );
	const float yaw = XMConvertToRadians( angles.azimuth + northOffset );
	return XMFLOAT3( std::cos( elevation ) * std::sin( yaw ), std::sin( elevation ), std::cos( elevation ) * std::cos( yaw ) );
}

XMFLOAT3 SunPosition::toSun() const
{
	return direction( angles(), m_properties["North offset"].data<float>() );
}

SunPosition::Moon SunPosition::moon() const
{
	return computeMoon( settings() );
}

XMFLOAT3 SunPosition::toMoon() const
{
	return direction( moon().angles, m_properties["North offset"].data<float>() );
}

void SunPosition::equatorialFrame( XMFLOAT3& x, XMFLOAT3& y, XMFLOAT3& z ) const
{
	const Settings current = settings();
	const double localSiderealTime = greenwichSiderealTime( julianDate( current ) ) + radians( current.longitude );
	const double latitude = radians( current.latitude );
	// Точка неба с прямым восхождением α и склонением δ: часовой угол — звёздное время места минус α
	auto world = [&]( double rightAscension, double declination )
	{
		return direction( horizontal( localSiderealTime - rightAscension, declination, latitude ), current.northOffset );
	};
	x = world( 0.0, 0.0 );
	y = world( 0.5 * pi, 0.0 );
	z = world( 0.0, 0.5 * pi );
}

SunPosition::Moon SunPosition::computeMoon( const Settings& settings )
{
	// Meeus, «Astronomical Algorithms», гл. 47: средние элементы орбиты и главные члены рядов (10 по долготе,
	// 6 по широте, 4 по расстоянию) — точность ~0,05°. Время — юлианские столетия от J2000.0 (TT ≈ UT)
	const double jd = julianDate( settings );
	const double t = ( jd - 2451545.0 ) / 36525.0;
	auto angle = []( double degreesValue ) { return radians( std::fmod( degreesValue, 360.0 ) ); };
	const double meanLongitude = angle( 218.3164477 + 481267.88123421 * t );
	const double d = angle( 297.8501921 + 445267.1114034 * t );		// средняя элонгация
	const double m = angle( 357.5291092 + 35999.0502909 * t );		// средняя аномалия солнца
	const double mp = angle( 134.9633964 + 477198.8675055 * t );		// средняя аномалия луны
	const double f = angle( 93.2720950 + 483202.0175233 * t );		// аргумент широты
	const double e = 1.0 - 0.002516 * t;
	const double longitude = meanLongitude + radians(
		6.288774 * std::sin( mp ) + 1.274027 * std::sin( 2.0 * d - mp ) + 0.658314 * std::sin( 2.0 * d ) +
		0.213618 * std::sin( 2.0 * mp ) - 0.185116 * e * std::sin( m ) - 0.114332 * std::sin( 2.0 * f ) +
		0.058793 * std::sin( 2.0 * d - 2.0 * mp ) + 0.057066 * e * std::sin( 2.0 * d - m - mp ) +
		0.053322 * std::sin( 2.0 * d + mp ) + 0.045758 * e * std::sin( 2.0 * d - m ) );
	const double latitude = radians(
		5.128122 * std::sin( f ) + 0.280602 * std::sin( mp + f ) + 0.277693 * std::sin( mp - f ) +
		0.173237 * std::sin( 2.0 * d - f ) + 0.055413 * std::sin( 2.0 * d - mp + f ) + 0.046271 * std::sin( 2.0 * d - mp - f ) );
	const double distance = 385000.56 - 20905.355 * std::cos( mp ) - 3699.111 * std::cos( 2.0 * d - mp ) -
							2955.968 * std::cos( 2.0 * d ) - 569.925 * std::cos( 2.0 * mp );

	// Эклиптика → экватор → горизонт наблюдателя
	double sunLongitude, obliquity, sunMeanLongitude, sunMeanAnomaly;
	solarCoordinates( t, sunLongitude, obliquity, sunMeanLongitude, sunMeanAnomaly );
	const double rightAscension = std::atan2( std::sin( longitude ) * std::cos( obliquity ) - std::tan( latitude ) * std::sin( obliquity ),
											  std::cos( longitude ) );
	const double declination = std::asin( std::sin( latitude ) * std::cos( obliquity ) +
										  std::cos( latitude ) * std::sin( obliquity ) * std::sin( longitude ) );
	const double hourAngle = greenwichSiderealTime( jd ) + radians( settings.longitude ) - rightAscension;

	Moon moon;
	moon.angles = horizontal( hourAngle, declination, radians( settings.latitude ) );
	// Параллакс: наблюдатель на поверхности, а не в центре Земли — луна ниже на π·cos h (π — ~0,95°)
	const double parallax = std::asin( 6378.14 / distance );
	moon.angles.elevation -= static_cast<float>( degrees( parallax * std::cos( radians( moon.angles.elevation ) ) ) );
	moon.distance = static_cast<float>( distance );
	moon.eclipticLongitude = static_cast<float>( degrees( std::fmod( longitude, 2.0 * pi ) + ( longitude < 0.0 ? 2.0 * pi : 0.0 ) ) );
	moon.eclipticLatitude = static_cast<float>( degrees( latitude ) );

	// Фаза (Meeus, гл. 48): элонгация от солнца и угол фазы; освещённость — по звёздной величине луны
	// (m = −12,73 + 0,026·i + 4·10⁻⁹·i⁴, Allen) и расстоянию; освещённость от звезды m — 10^(−0,4·(m + 14,18)) лк
	const double sunDistance = 149597870.7 * ( 1.00014 - 0.01671 * std::cos( sunMeanAnomaly ) );
	const double elongation = std::acos( std::clamp( std::cos( latitude ) * std::cos( longitude - sunLongitude ), -1.0, 1.0 ) );
	const double phaseAngle = std::atan2( sunDistance * std::sin( elongation ), distance - sunDistance * std::cos( elongation ) );
	const double phaseDegrees = degrees( phaseAngle );
	moon.phaseAngle = static_cast<float>( phaseDegrees );
	moon.illuminatedFraction = static_cast<float>( 0.5 * ( 1.0 + std::cos( phaseAngle ) ) );
	const double distanceFactor = ( 384400.0 / distance ) * ( 384400.0 / distance );
	auto illuminance = []( double magnitude ) { return std::pow( 10.0, -0.4 * ( magnitude + 14.18 ) ); };
	moon.illuminance = static_cast<float>( illuminance( -12.73 + 0.026 * phaseDegrees + 4e-9 * std::pow( phaseDegrees, 4.0 ) ) * distanceFactor );
	moon.fullIlluminance = static_cast<float>( illuminance( -12.73 ) * distanceFactor );
	return moon;
}

PropertyContainer* SunPosition::properties()
{
	return &m_properties;
}

SunPosition::Angles SunPosition::compute( const Settings& settings )
{
	// В double: юлианский день ~2,5 млн, во float от суток осталась бы четверть
	const double hours = settings.timeOfDay;
	const double jd = julianDate( settings );
	const double t = ( jd - 2451545.0 ) / 36525.0;	// юлианские столетия от J2000.0

	// Орбита Земли, видимая долгота солнца (уравнение центра, нутация, аберрация), наклон эклиптики и склонение
	double apparentLongitude, obliquity, meanLongitude, meanAnomaly;
	solarCoordinates( t, apparentLongitude, obliquity, meanLongitude, meanAnomaly );
	const double eccentricity = 0.016708634 - t * ( 0.000042037 + 0.0000001267 * t );
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

	return horizontal( hourAngle, declination, radians( settings.latitude ) );
}
