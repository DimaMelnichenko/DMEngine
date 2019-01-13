#pragma once
#include <fstream>
#include <memory>
#include <string>

class Logger
{
	Logger();
public:
	static Logger& instance();
	~Logger();
	void write(const std::string&);
	void write( const char* function, long line, const std::string& message );

private:
	std::ofstream m_fileOut;
	static std::unique_ptr<Logger> m_instance;
};

Logger& getLogger();

#define LOG( x ) \
	getLogger().write( __FUNCTION__, __LINE__, x );