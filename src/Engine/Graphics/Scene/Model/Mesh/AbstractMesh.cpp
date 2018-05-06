#include "AbstractMesh.h"

namespace GS
{
AbstractMesh::AbstractMesh( uint32_t id, const std::string& name, std::vector<uint32_t>&& indices ) 
	: DMResource( id, name ),
	m_indices( std::move( indices ) )
{

}

AbstractMesh::AbstractMesh( AbstractMesh&& other ) : DMResource( std::move( other ) )
{
	*this = std::move( other );
}
AbstractMesh& AbstractMesh::operator=( AbstractMesh&& other )
{
	m_vertex_combination = other.m_vertex_combination;
	std::swap( m_indices, other.m_indices );
	return *this;
}

AbstractMesh::~AbstractMesh()
{
}

VertexData::Type AbstractMesh::vertexType() const
{
	return m_vertex_combination;
}

uint32_t AbstractMesh::vertexOffset() const
{
	return m_vertexOffset;
}

uint32_t AbstractMesh::indexOffset() const
{
	return m_indexOffset;
}

uint32_t AbstractMesh::indexCount() const
{
	return m_indices.size();
}

void AbstractMesh::setOffsets( uint32_t vertexOffset, uint32_t indexoffset )
{
	m_vertexOffset = vertexOffset;
	m_indexOffset = indexoffset;
}

const std::vector<uint32_t>& AbstractMesh::getIndices() const
{
	return m_indices;
}

}