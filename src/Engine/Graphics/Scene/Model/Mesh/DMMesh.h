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
		if( !m_vertices.empty() )
		{
			DirectX::BoundingBox::CreateFromPoints( m_bounds, m_vertices.size(), &m_vertices[0].position,
													sizeof( VertexTypeStruct ) );
			const float bottom = m_bounds.Center.y - m_bounds.Extents.y;
			const float top = bottom + baseBand( 2.0f * m_bounds.Extents.y );
			std::vector<DirectX::XMFLOAT3> base;
			for( const VertexTypeStruct& vertex : m_vertices )
				if( vertex.position.y <= top )
					base.push_back( vertex.position );
			DirectX::BoundingBox::CreateFromPoints( m_baseBounds, base.size(), base.data(), sizeof( DirectX::XMFLOAT3 ) );
		}
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
		return reinterpret_cast<char*>( &m_vertices[0] );
	}

	uint32_t vertexCount() const override
	{
		return m_vertices.size();
	}

private:
	std::vector<VertexTypeStruct> m_vertices;
};

}

