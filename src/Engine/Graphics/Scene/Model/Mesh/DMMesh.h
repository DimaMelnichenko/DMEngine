#pragma once
#include "Utils\utilites.h"
#include "AbstractMesh.h"


namespace GS
{

template<typename VertexTypeStruct> // VertexData structs
class DMMesh : public AbstractMesh
{
public:
	DMMesh( uint32_t id, const std::string& name, 
			std::vector<uint32_t>&& indices, 
			std::vector<VertexTypeStruct>&& vertices ) :
		AbstractMesh( id, name, std::move(indices) ),
		m_vertices( std::move( vertices ) )
	{
		m_vertex_combination = VertexData::type<VertexTypeStruct>();
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

	char* getVertices() override
	{
		return  reinterpret_cast<char*>( &m_vertices[0] );
	}

	uint32_t vertexCount() const override
	{
		return m_vertices.size();
	}

private:
	std::vector<VertexTypeStruct> m_vertices;
};

}

