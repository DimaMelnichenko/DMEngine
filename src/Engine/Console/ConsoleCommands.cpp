#include "ConsoleCommands.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace GS
{

ConsoleReply::ConsoleReply( std::function<void( const std::string& )> complete ) : m_complete( std::move( complete ) )
{
}

ConsoleReply::~ConsoleReply()
{
	// Команда забыла ответить: клиент не должен ждать вечно
	if( !m_finished )
		finish( "error: command did not reply" );
}

void ConsoleReply::line( const std::string& text )
{
	m_text += text + "\n";
}

void ConsoleReply::ok( const std::string& text )
{
	if( !text.empty() )
		line( text );
	finish( "ok" );
}

void ConsoleReply::error( const std::string& message )
{
	finish( "error: " + message );
}

void ConsoleReply::finish( const std::string& status )
{
	if( m_finished )
		return;
	m_finished = true;
	if( m_complete )
		m_complete( m_text + status + "\n" );
}

ConsoleCommands::ConsoleCommands()
{
	registerCommand( "help", "list of commands", [this]( const std::vector<std::string>&, const ConsoleReplyPtr& reply )
	{
		for( const auto& [name, command] : m_commands )
			reply->line( name + " " + command.usage );
		reply->ok();
	} );

	registerCommand( "wait", "<frames> - wait for the given number of frames", [this]( const std::vector<std::string>& args,
																					  const ConsoleReplyPtr& reply )
	{
		int frames = args.empty() ? 1 : std::atoi( args[0].c_str() );
		if( frames <= 0 )
			return reply->error( "wait: expected a positive number of frames" );
		addFrameTask( [reply, frames]() mutable
		{
			if( --frames > 0 )
				return false;
			reply->ok();
			return true;
		} );
	} );
}

void ConsoleCommands::registerCommand( const std::string& name, const std::string& usage, Handler handler )
{
	m_commands[name] = { usage, std::move( handler ) };
}

void ConsoleCommands::execute( const std::string& line, const ConsoleReplyPtr& reply )
{
	std::vector<std::string> words = split( line );
	if( words.empty() )
		return reply->error( "empty command" );

	std::string name = words[0];
	std::transform( name.begin(), name.end(), name.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	auto it = m_commands.find( name );
	if( it == m_commands.end() )
		return reply->error( "unknown command " + words[0] + ", see help" );

	words.erase( words.begin() );
	it->second.handler( words, reply );
}

void ConsoleCommands::addFrameTask( std::function<bool()> task )
{
	m_frameTasks.push_back( std::move( task ) );
}

void ConsoleCommands::tick()
{
	// Задача может добавить новую: обходим копию
	std::vector<std::function<bool()>> tasks;
	tasks.swap( m_frameTasks );
	for( auto& task : tasks )
	{
		if( !task() )
			m_frameTasks.push_back( std::move( task ) );
	}
}

std::vector<std::string> ConsoleCommands::split( const std::string& line )
{
	std::vector<std::string> words;
	std::string word;
	bool quoted = false;
	bool hasWord = false;
	for( char c : line )
	{
		if( c == '"' )
		{
			quoted = !quoted;
			hasWord = true;
		}
		else if( !quoted && ( c == ' ' || c == '\t' || c == '\r' || c == '\n' ) )
		{
			if( hasWord )
				words.push_back( word );
			word.clear();
			hasWord = false;
		}
		else
		{
			word += c;
			hasWord = true;
		}
	}
	if( hasWord )
		words.push_back( word );
	return words;
}

std::vector<std::string> ConsoleCommands::names() const
{
	std::vector<std::string> result;
	for( const auto& [name, command] : m_commands )
		result.push_back( name );
	return result;
}

}
