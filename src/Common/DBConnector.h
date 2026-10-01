#pragma once

#include <memory>
#include "SQLiteCpp\SQLiteCpp.h"

// Соединение с base.db3 — синглтон, как DMD3D::instance(); init() открывает базу (DMSystem)
class DBConnector
{
private:
	DBConnector(){}
public:
	static DBConnector& instance();
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