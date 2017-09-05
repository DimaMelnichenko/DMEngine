#include "DMTimer.h"


DMTimer::DMTimer(  )
{
	m_total_time = 0;
}


DMTimer::~DMTimer()
{

}

bool DMTimer::Initialize( )
{
	// Check to see if this system supports high performance timers.
	QueryPerformanceFrequency( (LARGE_INTEGER*)&m_frequency );
	if( m_frequency == 0 )
	{
		return false;
	}

	// Find out how many times the frequency counter ticks every millisecond.
	m_ticksPerMs = (double)( m_frequency / 1000.0 );

	QueryPerformanceCounter( (LARGE_INTEGER*)&m_startTime );

	return true;
}

void DMTimer::Frame( )
{
	INT64 currentTime;

	double timeDifference;

	QueryPerformanceCounter( (LARGE_INTEGER*)& currentTime );

	timeDifference = static_cast<double>( currentTime - m_startTime );

	m_frameTime = timeDifference / m_ticksPerMs;

	m_startTime = currentTime;

	m_total_time += m_frameTime;

	return;
}

double DMTimer::GetTime( )
{
	return m_frameTime;
}

double DMTimer::totalTime()
{
	return m_total_time;
}