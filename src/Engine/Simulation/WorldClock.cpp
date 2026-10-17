#include "WorldClock.h"
#include <algorithm>

namespace GS
{

int32_t WorldClock::daysInMonth( int32_t year, int32_t month )
{
	static const int32_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	const bool leap = ( year % 4 == 0 && year % 100 != 0 ) || year % 400 == 0;
	return month == 2 && leap ? 29 : days[std::clamp( month, 1, 12 ) - 1];
}

void WorldClock::setHours( double hours )
{
	m_hours = std::clamp( hours, 0.0, 24.0 );
}

void WorldClock::setDate( const Date& date )
{
	m_date.year = date.year;
	m_date.month = std::clamp( date.month, 1, 12 );
	m_date.day = std::clamp( date.day, 1, daysInMonth( m_date.year, m_date.month ) );
}

void WorldClock::advance( double seconds )
{
	if( seconds <= 0.0 )
		return;
	if( m_gameTimeRunning )
		m_gameSeconds += seconds;
	if( m_timeScale <= 0.0f || m_paused )
		return;

	m_hours += seconds * m_timeScale / 3600.0;
	// Через полночь — следующий день: дата двигает склонение солнца, за год — сезоны
	while( m_hours >= 24.0 )
	{
		m_hours -= 24.0;
		if( ++m_date.day > daysInMonth( m_date.year, m_date.month ) )
		{
			m_date.day = 1;
			if( ++m_date.month > 12 )
			{
				m_date.month = 1;
				++m_date.year;
			}
		}
	}
}

}
