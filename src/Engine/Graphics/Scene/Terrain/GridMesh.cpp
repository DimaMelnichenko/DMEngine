#include "GridMesh.h"

using namespace DirectX;

namespace GS
{

void GridMesh::initialize( uint16_t width, uint16_t height )
{
	std::vector<XMFLOAT3> vertices;

	for( uint16_t j = 0; j < height; j++ )
	{
		for( uint16_t i = 0; i < width; i++ )
		{
			vertices.emplace_back( i, 0.0f, j );
		}
	}

	std::vector<unsigned long> indexes;

	for( unsigned long i = width; i < vertices.size(); i++ )
	{
		if( i % width )
		{
			indexes.push_back( i );
			indexes.push_back( i - width - 1 );
			indexes.push_back( i - 1 );

			indexes.push_back( i );
			indexes.push_back( i - width );
			indexes.push_back( i - width - 1 );
		}
	}

	m_vertexCount = static_cast<uint32_t>( vertices.size() );
	DMD3D::instance().createVertexBuffer( m_vertexBuffer, &vertices[0], sizeof( XMFLOAT3 ) * m_vertexCount );

	m_indexCount = static_cast<uint32_t>( indexes.size() );
	DMD3D::instance().createIndexBuffer( m_indexBuffer, &indexes[0], sizeof( unsigned long ) * m_indexCount );
}

uint32_t GridMesh::indexCount()
{
	return m_indexCount;
}

}
