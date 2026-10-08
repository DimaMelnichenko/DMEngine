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


void Logger::write( const char* function, long line, const std::string& message )
{
	std::lock_guard<std::mutex> lock( m_mutex );
	m_fileOut << "func:" << function << ", line:" << line << "|" << message << std::endl;
	m_recent.push_back( std::string( function ) + ": " + message );
	if( m_recent.size() > recentCapacity )
		m_recent.pop_front();
	++m_total;
}

void Logger::recent( std::vector<std::string>& lines, uint64_t& first ) const
{
	std::lock_guard<std::mutex> lock( m_mutex );
	lines.assign( m_recent.begin(), m_recent.end() );
	first = m_total - m_recent.size();
}

uint64_t Logger::total() const
{
	std::lock_guard<std::mutex> lock( m_mutex );
	return m_total;
}