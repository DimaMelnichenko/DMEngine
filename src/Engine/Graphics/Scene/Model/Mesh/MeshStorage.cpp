#include "MeshStorage.h"

namespace GS
{

MeshStorage::MeshStorage( const std::string& path ) : DMResourceStorage( path ),
	m_vertexCount(0),
	m_indexCount(0)
{

}

MeshStorage::~MeshStorage()
{

}

uint32_t MeshStorage::vertexCount() const
{
	return m_vertexCount;
}

uint32_t MeshStorage::indexCount() const
{
	return m_indexCount;
}

bool MeshStorage::load( const std::string& name )
{

	if( exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + name;

	try
	{
		std::unique_ptr<AbstractMesh> mesh;
		mesh.reset( m_meshLoader.loadFromFile<VertexData::PTNTB>( fullPath, nextId() ) );
		m_vertexCount += mesh->vertexCount();
		m_indexCount += mesh->indexCount();
		insertResource( std::move( mesh ) );
	}
	catch( std::exception& e )
	{
		//TODO log this exception
		return false;
	}

	return true;
}

}