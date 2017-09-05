#pragma once

/////////////
// LINKING //
/////////////
#pragma comment(lib, "pdh.lib")

//////////////
// INCLUDES //
//////////////
#include <pdh.h>

class DMCpu
{
public:
	DMCpu(  );
	~DMCpu();

	void Initialize( );
	void Shutdown( );
	void Frame( );
	int GetCpuPercentage( );

private:
	bool m_canReadCpu;
	HQUERY m_queryHandle;
	HCOUNTER m_counterHandle;
	unsigned long m_lastSampleTime;
	long m_cpuUsage;
};

