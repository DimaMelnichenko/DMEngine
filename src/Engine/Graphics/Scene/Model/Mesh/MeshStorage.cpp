#include "MeshStorage.h"
#include "Logger\Logger.h"

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

bool MeshStorage::load( uint32_t id, const std::string& name, const std::string& file )
{

	if( exists( name ) || exists( id ) )
		return true;

	std::string fullPath = path() + "\\" + file;

	try
	{
		std::unique_ptr<AbstractMesh> mesh;
		mesh.reset( m_meshLoader.loadFromFile<VertexData::PTNTB>( id, name, fullPath ) );
		m_vertexCount += mesh->vertexCount();
		m_indexCount += mesh->indexCount();
		insertResource( std::move( mesh ) );
	}
	catch( std::exception& e )
	{
		LOG( "Can`t load mesh from file: " + fullPath );
		return false;
	}

	return true;
}

}