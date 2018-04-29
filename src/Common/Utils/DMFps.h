#pragma once

#include <Windows.h>

class DMFps
{
public:
	DMFps(  );
	~DMFps();

	void Initialize( );
	void Frame( );
	int GetFps( );

private:
	int m_fps, m_count;
	unsigned long m_startTime;
};

