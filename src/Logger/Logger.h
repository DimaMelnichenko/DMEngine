#pragma once
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Logger
{
	Logger();
public:
	static Logger& instance();
	~Logger();
	void write( const char* function, long line, const std::string& message );

	// Последние строки лога (не больше recentCapacity) — панель Log редактора; first — номер первой из них с начала
	// запуска: по нему панель видит, что пришло новое. Пишут и поток удалённого управления, поэтому под мьютексом
	static constexpr size_t recentCapacity = 2000;
	void recent( std::vector<std::string>& lines, uint64_t& first ) const;
	uint64_t total() const;

private:
	std::ofstream m_fileOut;
	static std::unique_ptr<Logger> m_instance;
	mutable std::mutex m_mutex;
	std::deque<std::string> m_recent;
	uint64_t m_total = 0;
};

Logger& getLogger();

#define LOG( x ) \
	getLogger().write( __FUNCTION__, __LINE__, x );
