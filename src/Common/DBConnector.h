#pragma once

#include <memory>
#include "SQLiteCpp\SQLiteCpp.h"

class DBConnector
{
private:
	friend DBConnector& dbConnect();
	DBConnector(){}
public:
	DBConnector( const DBConnector& ) = delete;
	~DBConnector(){}

	bool init();

	SQLite::Database& db()
	{
		return *m_db;
	}

private:
	std::unique_ptr<SQLite::Database> m_db;
};