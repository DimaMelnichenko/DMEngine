#include "DBConnector.h"
#include <iostream>

namespace
{
	static std::unique_ptr<DBConnector> singletonConnector;
}

DBConnector& dbConnect()
{
	if( singletonConnector == nullptr )
	{
		singletonConnector.reset( new DBConnector() );
	}

	return *singletonConnector;
}

bool DBConnector::init()
{
	try
	{
		if( m_db == nullptr )
			m_db.reset( new SQLite::Database( "base.db3", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE ) );
	}
	catch( std::exception e )
	{
		std::cout << e.what();
		return false;
	}


	std::string msg( m_db->getErrorMsg() );

	return true;
}