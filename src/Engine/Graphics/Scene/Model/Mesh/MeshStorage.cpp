#include "MeshStorage.h"

namespace GS
{

MeshStorage::MeshStorage( const std::string& path ) : DMResourceStorage( path )
{

}

MeshStorage::~MeshStorage()
{

}

bool MeshStorage::load( const std::string& name )
{

	if( exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + name;

	try
	{
		std::unique_ptr<AbstractMesh> mesh;
		mesh.reset( m_meshLoader.loadFromFile<MeshVertexData::PTNTB>( fullPath, nextId() ) );
		insertResource( name, std::move( mesh ) );
	}
	catch( std::exception& e )
	{
		//TODO log this exception
		return false;
	}

	return true;
}

}