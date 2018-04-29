#pragma once

//////////////
// INCLUDES //
//////////////
#include <windows.h>

class DMTimer
{
public:
	DMTimer(  );
	~DMTimer();

	bool Initialize( );
	void Frame( );

	double GetTime( );
	double totalTime();

private:
	INT64 m_frequency;
	double m_ticksPerMs;
	INT64 m_startTime;
	double m_frameTime;
	double m_total_time;
};

