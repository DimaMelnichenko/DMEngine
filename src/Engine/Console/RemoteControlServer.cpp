#include "RemoteControlServer.h"
#include <chrono>
#include "Logger\Logger.h"

namespace GS
{

namespace
{

constexpr DWORD bufferSize = 64 * 1024;

HANDLE createPipe()
{
	// Один экземпляр: второй запущенный движок канал не получит (FILE_FLAG_FIRST_PIPE_INSTANCE)
	return CreateNamedPipeW( RemoteControlServer::pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
							 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, bufferSize, bufferSize,
							 0, nullptr );
}

}

RemoteControlServer::~RemoteControlServer()
{
	stop();
}

bool RemoteControlServer::start()
{
	// Первый экземпляр канала — в главном потоке: ошибку (канал занят другим движком) видно в логе
	HANDLE pipe = createPipe();
	if( pipe == INVALID_HANDLE_VALUE )
	{
		LOG( "Remote control: can`t create pipe, error " + std::to_string( GetLastError() ) + " (another DMEngine with -remote?)" );
		return false;
	}
	m_stopEvent = CreateEventW( nullptr, TRUE, FALSE, nullptr );
	m_ioEvent = CreateEventW( nullptr, TRUE, FALSE, nullptr );
	if( !m_stopEvent || !m_ioEvent )
	{
		CloseHandle( pipe );
		return false;
	}
	m_thread = std::thread( [this, pipe]
	{
		HANDLE current = pipe;
		while( current != INVALID_HANDLE_VALUE )
		{
			serve( current );
			CloseHandle( current );
			current = WaitForSingleObject( m_stopEvent, 0 ) == WAIT_OBJECT_0 ? INVALID_HANDLE_VALUE : createPipe();
		}
	} );
	LOG( "Remote control: listening on \\\\.\\pipe\\DMEngine" );
	return true;
}

void RemoteControlServer::stop()
{
	if( !m_thread.joinable() )
		return;
	SetEvent( m_stopEvent );
	m_thread.join();
	CloseHandle( m_stopEvent );
	CloseHandle( m_ioEvent );
	m_stopEvent = m_ioEvent = nullptr;
}

void RemoteControlServer::poll( ConsoleCommands& commands )
{
	std::deque<Request> requests;
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		requests.swap( m_requests );
	}
	for( Request& request : requests )
	{
		LOG( "Remote control: " + request.line );
		auto promise = std::make_shared<std::promise<std::string>>( std::move( request.reply ) );
		commands.execute( request.line, std::make_shared<ConsoleReply>( [promise]( const std::string& text )
		{
			promise->set_value( text );
		} ) );
	}
}

void RemoteControlServer::serve( HANDLE pipe )
{
	OVERLAPPED overlapped = {};
	overlapped.hEvent = m_ioEvent;
	DWORD bytes = 0;
	if( !ConnectNamedPipe( pipe, &overlapped ) )
	{
		const DWORD error = GetLastError();
		if( error == ERROR_IO_PENDING )
		{
			if( !waitIo( pipe, overlapped, bytes, true ) )
				return;
		}
		else if( error != ERROR_PIPE_CONNECTED )
		{
			return;
		}
	}

	// Команды — строки до \n; соединение живёт, пока клиент не закроет канал
	std::string buffer;
	char chunk[4096];
	while( true )
	{
		overlapped = {};
		overlapped.hEvent = m_ioEvent;
		if( !ReadFile( pipe, chunk, sizeof( chunk ), nullptr, &overlapped ) && GetLastError() != ERROR_IO_PENDING )
			break;	// клиент отключился
		if( !waitIo( pipe, overlapped, bytes, true ) )
			break;
		buffer.append( chunk, bytes );

		for( size_t end = buffer.find( '\n' ); end != std::string::npos; end = buffer.find( '\n' ) )
		{
			std::string line = buffer.substr( 0, end );
			buffer.erase( 0, end + 1 );
			if( !line.empty() && line.back() == '\r' )
				line.pop_back();
			if( line.empty() )
				continue;

			std::string reply;
			if( !submit( line, reply ) )
				return;
			overlapped = {};
			overlapped.hEvent = m_ioEvent;
			if( !WriteFile( pipe, reply.data(), static_cast<DWORD>( reply.size() ), nullptr, &overlapped ) &&
				GetLastError() != ERROR_IO_PENDING )
				return;
			if( !waitIo( pipe, overlapped, bytes, false ) )
				return;
		}
	}
	DisconnectNamedPipe( pipe );
}

bool RemoteControlServer::submit( const std::string& line, std::string& reply )
{
	std::future<std::string> future;
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		m_requests.push_back( { line, {} } );
		future = m_requests.back().reply.get_future();
	}
	while( future.wait_for( std::chrono::milliseconds( 20 ) ) != std::future_status::ready )
	{
		if( WaitForSingleObject( m_stopEvent, 0 ) == WAIT_OBJECT_0 )
			return false;
	}
	reply = future.get();
	return true;
}

bool RemoteControlServer::waitIo( HANDLE pipe, OVERLAPPED& overlapped, DWORD& bytes, bool stoppable )
{
	// Запись не прерывается остановкой, чтобы ответ quit дошёл до клиента, но и не ждёт вечно
	HANDLE handles[2] = { overlapped.hEvent, m_stopEvent };
	const DWORD result = WaitForMultipleObjects( stoppable ? 2 : 1, handles, FALSE, stoppable ? INFINITE : 5000 );
	if( result != WAIT_OBJECT_0 )
	{
		CancelIoEx( pipe, &overlapped );
		GetOverlappedResult( pipe, &overlapped, &bytes, TRUE );
		return false;
	}
	return GetOverlappedResult( pipe, &overlapped, &bytes, FALSE ) != FALSE;
}

}
