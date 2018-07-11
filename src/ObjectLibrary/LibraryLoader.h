#pragma once
#include <memory>
#include "SQLiteCpp\SQLiteCpp.h"

class LibraryLoader
{
public:
	LibraryLoader();
	~LibraryLoader();
	
	void load();

private:
	std::unique_ptr<SQLite::Database> m_db;
};

