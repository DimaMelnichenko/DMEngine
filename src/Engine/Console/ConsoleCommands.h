#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GS
{

// Ответ консольной команды: строки текста и завершение — ok или error. Команда на несколько кадров (снимок, замер
// времени) держит ответ у себя и завершает его позже; complete вызывается один раз, в главном потоке
class ConsoleReply
{
public:
	explicit ConsoleReply( std::function<void( const std::string& )> complete );
	~ConsoleReply();

	void line( const std::string& text );
	void ok( const std::string& text = {} );
	void error( const std::string& message );
	bool finished() const { return m_finished; }

private:
	void finish( const std::string& status );

	std::string m_text;
	std::function<void( const std::string& )> m_complete;
	bool m_finished = false;
};

using ConsoleReplyPtr = std::shared_ptr<ConsoleReply>;

// Консольные команды, как IConsoleManager в UE: имя → обработчик. Строку команды присылает удалённое управление
// (RemoteControlServer, Tools/engine.py); выполняется она в главном потоке, в начале кадра. Обработчик получает слова
// строки без имени команды (в кавычках — с пробелами) и ответ: завершает его сразу или позже (tick, точка кадра)
class ConsoleCommands
{
public:
	using Handler = std::function<void( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )>;

	ConsoleCommands();

	// usage — строка справки: аргументы и что делает команда
	void registerCommand( const std::string& name, const std::string& usage, Handler handler );
	void execute( const std::string& line, const ConsoleReplyPtr& reply );

	// Задача на несколько кадров: вызывается в начале каждого кадра, пока не вернёт true
	void addFrameTask( std::function<bool()> task );
	void tick();

	static std::vector<std::string> split( const std::string& line );
	// Имена команд по алфавиту — дополнение в консоли редактора
	std::vector<std::string> names() const;

private:
	struct Command
	{
		std::string usage;
		Handler handler;
	};

	std::map<std::string, Command> m_commands;
	std::vector<std::function<bool()>> m_frameTasks;
};

}
