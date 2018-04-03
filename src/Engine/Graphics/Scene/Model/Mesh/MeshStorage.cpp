#include "MeshStorage.h"

namespace GS
{

MeshStorage::MeshStorage( const std::wstring& path ) : DMResourceStorage( path )
{

}

MeshStorage::~MeshStorage()
{

}

bool MeshStorage::load( const std::wstring& name )
{

	if( exists( name ) )
		return true;

	std::wstring fullPath = path() + L"\\" + name;

	try
	{
		insertResource( name, std::move( m_meshLoader.loadFromFile<MeshVertexData::PTNTB>( fullPath, nextId() ) ) );
	}
	catch( std::exception& e )
	{
		//TODO log this exception
		return false;
	}

	return true;
}

}