#include "Logger.h"


std::unique_ptr<Logger> Logger::m_instance = nullptr;

Logger& getLogger()
{
	return Logger::instance();
}

Logger& Logger::instance()
{
	if (!m_instance)
		m_instance.reset(new Logger());

	return *m_instance;
}

Logger::Logger()
{
	m_fileOut.open( "log.txt", std::ios_base::out | std::ios_base::trunc );// | std::ios_base::app);
	
	m_fileOut << "\n\n";
	m_fileOut << "----------------------------------------------------------" << "\n";
	m_fileOut << "\t\t\t\tInitialize logger" << "\n";
	m_fileOut << "\n\n";
}


Logger::~Logger()
{
	if( m_fileOut.is_open() )
	{
		m_fileOut << "\n\nClose logger\n";
		m_fileOut << "\n<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n";
		m_fileOut.close();
	}
}


void Logger::write( const std::string& message )
{
	m_fileOut << message << "\n";
}

void Logger::write( const char* function, long line, const std::string& message )
{
	m_fileOut << "func:" << function << ", line:" << line << "|" << message << std::endl;
}