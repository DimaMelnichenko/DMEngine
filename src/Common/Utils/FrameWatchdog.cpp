#include "FrameWatchdog.h"
#include <cstdio>
#include <cstring>
#include "Logger\Logger.h"

FrameWatchdog& FrameWatchdog::instance()
{
	static FrameWatchdog watchdog;
	return watchdog;
}

FrameWatchdog::~FrameWatchdog()
{
	stop();
}

int64_t FrameWatchdog::now()
{
	return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

void FrameWatchdog::start( std::chrono::milliseconds limit, std::function<std::string()> probe )
{
	stop();
	m_limit = limit;
	m_probe = std::move( probe );
	m_stopping = false;
	m_thread = std::thread( [this] { run(); } );
}

void FrameWatchdog::stop()
{
	if( !m_thread.joinable() )
		return;
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		m_stopping = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

void FrameWatchdog::stage( const char* name, bool newFrame )
{
	if( newFrame )
		m_frame.fetch_add( 1, std::memory_order_relaxed );
	m_stageStart.store( now(), std::memory_order_relaxed );
	m_stage.store( name, std::memory_order_release );
}

void FrameWatchdog::event( const char* name )
{
	if( !m_thread.joinable() )
		return;
	std::lock_guard<std::mutex> lock( m_eventMutex );
	strncpy_s( m_event, name ? name : "", _TRUNCATE );
}

FrameWatchdog::Scope::Scope( const char* name ) :
	m_previous( FrameWatchdog::instance().m_stage.load( std::memory_order_relaxed ) ),
	m_previousStart( FrameWatchdog::instance().m_stageStart.load( std::memory_order_relaxed ) )
{
	FrameWatchdog::instance().stage( name );
}

FrameWatchdog::Scope::~Scope()
{
	FrameWatchdog& watchdog = FrameWatchdog::instance();
	watchdog.m_stageStart.store( m_previousStart, std::memory_order_relaxed );
	watchdog.m_stage.store( m_previous, std::memory_order_release );
}

void FrameWatchdog::run()
{
	const int64_t limit = std::chrono::duration_cast<std::chrono::microseconds>( m_limit ).count();
	int64_t stuckStart = 0;		// начало шага, о котором уже сказано (0 — кадры идут)
	int64_t lastReport = 0;
	std::unique_lock<std::mutex> lock( m_mutex );
	while( !m_wake.wait_for( lock, std::chrono::milliseconds( 500 ), [this] { return m_stopping; } ) )
	{
		const char* stage = m_stage.load( std::memory_order_acquire );
		const int64_t start = m_stageStart.load( std::memory_order_relaxed );
		const int64_t time = now();
		char text[256];
		if( stuckStart != 0 && start != stuckStart )
		{
			std::snprintf( text, sizeof( text ), "Frame watchdog: frame %llu goes on after %.1f s",
						   static_cast<unsigned long long>( m_frame.load( std::memory_order_relaxed ) ), ( time - stuckStart ) * 1e-6 );
			LOG( text );
			stuckStart = 0;
		}
		if( !stage || start == 0 || time - start < limit || ( stuckStart == start && time - lastReport < limit * 3 ) )
			continue;
		std::snprintf( text, sizeof( text ), "Frame watchdog: frame %llu stands for %.0f s at \"%s\"",
					   static_cast<unsigned long long>( m_frame.load( std::memory_order_relaxed ) ), ( time - start ) * 1e-6, stage );
		std::string event;
		{
			std::lock_guard<std::mutex> eventLock( m_eventMutex );
			event = m_event;
		}
		LOG( std::string( text ) + ( event.empty() ? "" : ", last event \"" + event + "\"" ) + ( m_probe ? "; " + m_probe() : std::string() ) );
		stuckStart = start;
		lastReport = time;
	}
}
