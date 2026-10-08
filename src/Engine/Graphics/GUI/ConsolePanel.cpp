#include "ConsolePanel.h"
#include <algorithm>
#include <cctype>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "Logger\Logger.h"
#include "PropertyWidgets.h"

namespace
{

const ImVec4 errorColor( 1.0f, 0.45f, 0.4f, 1.0f );
const ImVec4 warningColor( 1.0f, 0.8f, 0.35f, 1.0f );
const ImVec4 inputColor( 0.55f, 0.8f, 1.0f, 1.0f );

bool startsWithNoCase( const std::string& text, const std::string& prefix )
{
	if( prefix.size() > text.size() )
		return false;
	for( size_t i = 0; i < prefix.size(); ++i )
	{
		if( std::tolower( static_cast<unsigned char>( text[i] ) ) != std::tolower( static_cast<unsigned char>( prefix[i] ) ) )
			return false;
	}
	return true;
}

bool isError( const std::string& line )
{
	return PropertyWidgets::matches( line, "error" ) || PropertyWidgets::matches( line, "fail" ) || PropertyWidgets::matches( line, "can`t" );
}

bool isWarning( const std::string& line )
{
	return PropertyWidgets::matches( line, "warning" ) || PropertyWidgets::matches( line, "lazy" ) ||
		   PropertyWidgets::matches( line, "placeholder" );
}

}

void ConsolePanel::draw( bool* open, const std::vector<std::string>& commands, const std::vector<PropertyContainer*>& containers )
{
	m_commands = &commands;
	m_containers = &containers;
	if( !ImGui::Begin( "Output", open ) )
	{
		ImGui::End();
		return;
	}
	if( ImGui::BeginTabBar( "output" ) )
	{
		// Горячая клавиша консоли открывает вкладку Console
		if( ImGui::BeginTabItem( "Console", nullptr, m_focusInput ? ImGuiTabItemFlags_SetSelected : 0 ) )
		{
			drawConsole();
			ImGui::EndTabItem();
		}
		if( ImGui::BeginTabItem( "Log" ) )
		{
			drawLog();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void ConsolePanel::drawConsole()
{
	const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	if( ImGui::BeginChild( "output", ImVec2( 0.0f, -footer ), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar ) )
	{
		for( const Line& line : m_output )
		{
			if( line.kind == Kind::input )
				ImGui::TextColored( inputColor, "%s", line.text.c_str() );
			else if( line.kind == Kind::error )
				ImGui::TextColored( errorColor, "%s", line.text.c_str() );
			else
				ImGui::TextUnformatted( line.text.c_str() );
		}
		if( m_scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() )
			ImGui::SetScrollHereY( 1.0f );
		m_scrollToBottom = false;
	}
	ImGui::EndChild();
	ImGui::Separator();

	ImGui::SetNextItemWidth( -FLT_MIN );
	if( m_focusInput )
	{
		ImGui::SetKeyboardFocusHere();
		m_focusInput = false;
	}
	const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
									  ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_EscapeClearsAll;
	if( ImGui::InputTextWithHint( "##input", "Command (help — list, Tab — complete)", &m_input, flags, inputCallback, this ) )
	{
		if( !m_input.empty() )
		{
			m_output.push_back( { "> " + m_input, Kind::input } );
			m_pending.push_back( m_input );
			m_history.erase( std::remove( m_history.begin(), m_history.end(), m_input ), m_history.end() );
			m_history.push_back( m_input );
			m_input.clear();
			m_historyPosition = -1;
			m_scrollToBottom = true;
		}
		// Строка ввода остаётся в фокусе — следующая команда сразу
		ImGui::SetKeyboardFocusHere( -1 );
	}
}

void ConsolePanel::drawLog()
{
	// Новые строки — только когда лог вырос
	const uint64_t total = getLogger().total();
	if( total != m_logTotal )
	{
		uint64_t first = 0;
		getLogger().recent( m_logLines, first );
		m_logTotal = total;
	}
	ImGui::Checkbox( "Errors and warnings", &m_logErrorsOnly );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputTextWithHint( "##logFilter", "Search", &m_logFilter );
	if( ImGui::BeginChild( "log", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar ) )
	{
		const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
		for( const std::string& line : m_logLines )
		{
			const bool error = isError( line );
			const bool warning = !error && isWarning( line );
			if( m_logErrorsOnly && !error && !warning )
				continue;
			if( !PropertyWidgets::matches( line, m_logFilter ) )
				continue;
			if( error )
				ImGui::TextColored( errorColor, "%s", line.c_str() );
			else if( warning )
				ImGui::TextColored( warningColor, "%s", line.c_str() );
			else
				ImGui::TextUnformatted( line.c_str() );
		}
		if( atBottom )
			ImGui::SetScrollHereY( 1.0f );
	}
	ImGui::EndChild();
}

std::vector<std::string> ConsolePanel::takePending()
{
	std::vector<std::string> pending;
	pending.swap( m_pending );
	return pending;
}

void ConsolePanel::appendReply( const std::string& text )
{
	size_t start = 0;
	while( start < text.size() )
	{
		size_t end = text.find( '\n', start );
		if( end == std::string::npos )
			end = text.size();
		const std::string line = text.substr( start, end - start );
		// Статус ok — не печатается: ответ без строк и так понятен
		if( line.rfind( "error", 0 ) == 0 )
			m_output.push_back( { line, Kind::error } );
		else if( line != "ok" && !line.empty() )
			m_output.push_back( { line, Kind::text } );
		start = end + 1;
	}
	m_scrollToBottom = true;
}

int ConsolePanel::inputCallback( ImGuiInputTextCallbackData* data )
{
	ConsolePanel* panel = static_cast<ConsolePanel*>( data->UserData );
	if( data->EventFlag == ImGuiInputTextFlags_CallbackCompletion )
		panel->complete( data );
	else if( data->EventFlag == ImGuiInputTextFlags_CallbackHistory )
		panel->history( data );
	return 0;
}

void ConsolePanel::history( ImGuiInputTextCallbackData* data )
{
	if( m_history.empty() )
		return;
	const int previous = m_historyPosition;
	if( data->EventKey == ImGuiKey_UpArrow )
		m_historyPosition = m_historyPosition < 0 ? static_cast<int>( m_history.size() ) - 1 : std::max( m_historyPosition - 1, 0 );
	else if( data->EventKey == ImGuiKey_DownArrow && m_historyPosition >= 0 )
		m_historyPosition = m_historyPosition + 1 < static_cast<int>( m_history.size() ) ? m_historyPosition + 1 : -1;
	if( previous == m_historyPosition )
		return;
	data->DeleteChars( 0, data->BufTextLen );
	if( m_historyPosition >= 0 )
		data->InsertChars( 0, m_history[m_historyPosition].c_str() );
}

void ConsolePanel::propertyPaths( PropertyContainer& container, const std::string& path, bool windowsOnly,
								  std::vector<std::string>& paths ) const
{
	paths.push_back( path );
	if( !windowsOnly )
	{
		for( const std::string& name : container.names() )
			paths.push_back( path + "/" + name );
	}
	for( PropertyContainer* sub : container.subContainer() )
		propertyPaths( *sub, path + "/" + sub->name(), windowsOnly, paths );
}

void ConsolePanel::complete( ImGuiInputTextCallbackData* data )
{
	// Слово под курсором: команда (первое) или путь свойства (второе у get / set / list); путь может быть в кавычках
	const std::string text( data->Buf, data->CursorPos );
	const bool inQuotes = std::count( text.begin(), text.end(), '"' ) % 2 == 1;
	const size_t wordStart = inQuotes ? text.rfind( '"' ) : ( text.find_last_of( ' ' ) == std::string::npos ? 0 : text.find_last_of( ' ' ) + 1 );
	const std::string prefix = text.substr( inQuotes ? wordStart + 1 : wordStart );
	const std::string before = text.substr( 0, wordStart );
	const bool firstWord = before.find_first_not_of( ' ' ) == std::string::npos;

	std::vector<std::string> candidates;
	if( firstWord && m_commands )
	{
		for( const std::string& command : *m_commands )
		{
			if( startsWithNoCase( command, prefix ) )
				candidates.push_back( command );
		}
	}
	else if( m_containers )
	{
		const std::string command = before.substr( before.find_first_not_of( ' ' ), before.find( ' ', before.find_first_not_of( ' ' ) ) -
																					before.find_first_not_of( ' ' ) );
		if( command != "get" && command != "set" && command != "list" )
			return;
		std::vector<std::string> paths;
		for( PropertyContainer* container : *m_containers )
			propertyPaths( *container, container->name(), command == "list", paths );
		for( const std::string& path : paths )
		{
			if( startsWithNoCase( path, prefix ) )
				candidates.push_back( path );
		}
	}
	if( candidates.empty() )
		return;

	// Общее начало кандидатов; один кандидат — целиком, путь с пробелами — в кавычках
	std::string common = candidates[0];
	for( const std::string& candidate : candidates )
	{
		size_t n = 0;
		while( n < common.size() && n < candidate.size() &&
			   std::tolower( static_cast<unsigned char>( common[n] ) ) == std::tolower( static_cast<unsigned char>( candidate[n] ) ) )
			++n;
		common.resize( n );
	}
	std::string replacement = common;
	if( candidates.size() == 1 )
	{
		const bool quote = inQuotes || replacement.find( ' ' ) != std::string::npos;
		replacement = quote ? "\"" + replacement + "\" " : replacement + " ";
	}
	else
	{
		if( inQuotes || common.find( ' ' ) != std::string::npos )
			replacement = "\"" + replacement;
		m_output.push_back( { "> " + std::string( data->Buf ), Kind::input } );
		for( const std::string& candidate : candidates )
			m_output.push_back( { "  " + candidate, Kind::text } );
		m_scrollToBottom = true;
	}
	data->DeleteChars( static_cast<int>( wordStart ), data->CursorPos - static_cast<int>( wordStart ) );
	data->InsertChars( data->CursorPos, replacement.c_str() );
}
