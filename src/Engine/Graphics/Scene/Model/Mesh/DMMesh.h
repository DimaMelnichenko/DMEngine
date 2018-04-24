#pragma once
#include "Utils\utilites.h"
#include "DMResource.h"
#include "MeshVertexData.h"

namespace GS
{

class AbstractMesh : public DMResource
{
public:
	AbstractMesh() = default;
	AbstractMesh( uint32_t id, const std::string& name ) : DMResource( id, name )
	{

	}
	AbstractMesh( AbstractMesh&& other ) : DMResource( std::move(other) )
	{
		*this = std::move( other );
	}
	AbstractMesh& operator=( AbstractMesh&& other )
	{
		m_vertex_combination = other.m_vertex_combination;
	}
	~AbstractMesh()
	{
	}

	MeshVertexData::Type vertexType() const
	{
		return m_vertex_combination;
	}

protected:
	MeshVertexData::Type m_vertex_combination;
};


template<typename VertexTypeStruct> // MeshVertexData structs
class DMMesh : public AbstractMesh
{
public:
	DMMesh( uint32_t id, const std::string& name, std::vector<uint32_t>&& indices, std::vector<VertexTypeStruct>&& vertices ) :
		AbstractMesh( id, name ),
		m_indices( std::move( indices ) ),
		m_vertices( std::move( vertices ) )
	{
		m_vertex_combination = MeshVertexData::type<VertexTypeStruct>();
	}
	DMMesh( DMMesh&& other ) : AbstractMesh( std::move( other ) )
	{
		*this = std::move( other );
	}
	DMMesh& operator=( DMMesh&& other )
	{
		std::swap( m_indices, other.m_indices );
		std::swap( m_vertices, other.m_vertices );
	}
	~DMMesh(){}

	
	std::vector<VertexTypeStruct>& getVertices() const
	{
		return m_vertices;
	}
	std::vector<uint32_t>& getIndices() const
	{
		return m_indices;
	}

private:
	bool readFile( char* filename );

private:
	std::vector<uint32_t> m_indices;
	std::vector<VertexTypeStruct> m_vertices;
};

}

