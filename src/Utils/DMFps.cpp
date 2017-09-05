#include "DMFps.h"


DMFps::DMFps(  )
{
}


DMFps::~DMFps()
{
}

void DMFps::Initialize( )
{
	m_fps = 0;
	m_count = 0;
	m_startTime = timeGetTime( );
	return;
}

void DMFps::Frame( )
{
	m_count++;

	if( timeGetTime( ) >= ( m_startTime + 1000 ) )
	{
		m_fps = m_count;
		m_count = 0;

		m_startTime = timeGetTime( );
	}
}

int DMFps::GetFps( )
{
	return m_fps;
}