#include "FrameWatchdog.h"
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <dbghelp.h>
#include "Logger\Logger.h"

FrameWatchdog& FrameWatchdog::instance()
{
	static FrameWatchdog watchdog;
	return watchdog;
}

FrameWatchdog::~FrameWatchdog()
{
	stop();
	if( m_mainThread )
		CloseHandle( static_cast<HANDLE>( m_mainThread ) );
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
	if( !m_mainThread )
		DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &m_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS );
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
		if( stuckStart != start )
			logMainThreadStack();
		stuckStart = start;
		lastReport = time;
	}
}

void FrameWatchdog::logMainThreadStack()
{
	if( !m_mainThread )
		return;
	// Адреса — пока поток остановлен, без выделения памяти (кучу мог держать он): разворот по таблицам исключений x64
	DWORD64 frames[48];
	uint32_t count = 0;
	HANDLE thread = static_cast<HANDLE>( m_mainThread );
	if( SuspendThread( thread ) == static_cast<DWORD>( -1 ) )
		return;
	CONTEXT context = {};
	context.ContextFlags = CONTEXT_FULL;
	if( GetThreadContext( thread, &context ) )
	{
		while( count < 48 && context.Rip )
		{
			frames[count++] = context.Rip;
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION function = RtlLookupFunctionEntry( context.Rip, &imageBase, nullptr );
			if( !function )
			{
				// Листовая функция: адрес возврата — на вершине стека
				context.Rip = *reinterpret_cast<DWORD64*>( context.Rsp );
				context.Rsp += 8;
				continue;
			}
			void* handlerData = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind( UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr );
		}
	}
	ResumeThread( thread );

	// Символы — после: модули и их PDB (рядом с exe, у Debug — с номерами строк)
	HANDLE process = GetCurrentProcess();
	SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES );
	const bool symbols = SymInitialize( process, nullptr, TRUE ) != FALSE;
	LOG( "Frame watchdog: main thread stack" );
	for( uint32_t i = 0; i < count; ++i )
	{
		char line[512];
		char moduleName[MAX_PATH] = "?";
		HMODULE module = nullptr;
		if( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								reinterpret_cast<LPCSTR>( frames[i] ), &module ) )
		{
			GetModuleFileNameA( module, moduleName, MAX_PATH );
			if( const char* slash = strrchr( moduleName, '\\' ) )
				memmove( moduleName, slash + 1, strlen( slash + 1 ) + 1 );
		}
		alignas( SYMBOL_INFO ) char buffer[sizeof( SYMBOL_INFO ) + 256];
		SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>( buffer );
		symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
		symbol->MaxNameLen = 255;
		DWORD64 displacement = 0;
		IMAGEHLP_LINE64 source = {};
		source.SizeOfStruct = sizeof( source );
		DWORD lineDisplacement = 0;
		if( symbols && SymFromAddr( process, frames[i], &displacement, symbol ) )
		{
			if( SymGetLineFromAddr64( process, frames[i], &lineDisplacement, &source ) )
				std::snprintf( line, sizeof( line ), "  %2u. %s!%s + 0x%llx (%s:%lu)", i, moduleName, symbol->Name,
							   static_cast<unsigned long long>( displacement ), source.FileName, source.LineNumber );
			else
				std::snprintf( line, sizeof( line ), "  %2u. %s!%s + 0x%llx", i, moduleName, symbol->Name, static_cast<unsigned long long>( displacement ) );
		}
		else
			std::snprintf( line, sizeof( line ), "  %2u. %s 0x%llx", i, moduleName, static_cast<unsigned long long>( frames[i] ) );
		LOG( line );
	}
	if( symbols )
		SymCleanup( process );
}
