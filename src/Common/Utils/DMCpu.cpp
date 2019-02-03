#include "DMCpu.h"


DMCpu::DMCpu(  )
{
	m_queryHandle = 0;
}


DMCpu::~DMCpu()
{
	Shutdown();
}

void DMCpu::Initialize( )
{
	PDH_STATUS status;


	// Initialize the flag indicating whether this object can read the system cpu usage or not.
	m_canReadCpu = true;

	// Create a query object to poll cpu usage.
	status = PdhOpenQuery( nullptr, 0, &m_queryHandle );
	if( status != ERROR_SUCCESS )
	{
		m_canReadCpu = false;
	}

	// Set query object to poll all cpus in the system.
	status = PdhAddEnglishCounter( m_queryHandle, TEXT( "\\Processor(_Total)\\% processor time" ), 0, &m_counterHandle );
	if( status != ERROR_SUCCESS )
	{
		m_canReadCpu = false;
	}

	m_lastSampleTime = GetTickCount64( );

	m_cpuUsage = 0;

	return;
}

void DMCpu::Shutdown( )
{
	if( m_canReadCpu && m_queryHandle )
	{
		PdhCloseQuery( m_queryHandle );
	}

	return;
}

void DMCpu::Frame( )
{
	PDH_FMT_COUNTERVALUE value;

	if( m_canReadCpu )
	{
		if( ( m_lastSampleTime + 1000 ) < GetTickCount64( ) )
		{
			m_lastSampleTime = GetTickCount64( );

			PdhCollectQueryData( m_queryHandle );

			PdhGetFormattedCounterValue( m_counterHandle, PDH_FMT_LONG, nullptr, &value );

			m_cpuUsage = value.longValue;
		}
	}

	return;
}

int DMCpu::GetCpuPercentage( )
{
	int usage;

	if( m_canReadCpu )
	{
		usage = (int)m_cpuUsage;
	}
	else
	{
		usage = 0;
	}

	return usage;
}