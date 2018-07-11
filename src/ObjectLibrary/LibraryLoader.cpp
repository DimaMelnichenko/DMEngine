#include "LibraryLoader.h"
#include "System.h"



LibraryLoader::LibraryLoader()
{
}


LibraryLoader::~LibraryLoader()
{
}

void LibraryLoader::load()
{
	if( !m_db )
		m_db.reset( new SQLite::Database( "base.db3", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE ) );

	SQLite::Statement query( *m_db, "SELECT id, name, file FROM Textures" );
	while( query.executeStep() )
	{
		GS::System::textures().load( query.getColumn(0), query.getColumn(1), query.getColumn(2) );
	}
}