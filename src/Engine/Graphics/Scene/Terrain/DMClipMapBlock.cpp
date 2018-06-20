#include "DMClipMapBlock.h"


DMClipMapBlock::DMClipMapBlock()
{
}

DMClipMapBlock::~DMClipMapBlock()
{
}

void DMClipMapBlock::Initialize( uint16_t width, uint16_t height )
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

			if( i % width > ( height - 1 )
				&& i % width < ( width + 1 - height )
				&& i / width >( height - 1 )
				&& i / width < ( width + 1 - height ) )
			{
				continue;
			}
		}
	}

	m_vertexCount = vertices.size();
	DMD3D::instance().createVertexBuffer( m_vertexBuffer, &vertices[0], sizeof( XMFLOAT3 ) * m_vertexCount );

	m_indexCount = indexes.size();
	DMD3D::instance().createIndexBuffer( m_indexBuffer, &indexes[0], sizeof( unsigned long ) * m_indexCount );
}

ID3D11Buffer* DMClipMapBlock::vertexBuffer()
{
	return m_vertexBuffer.get();
}

ID3D11Buffer* DMClipMapBlock::indexBuffer()
{
	return m_indexBuffer.get();
}

uint32_t DMClipMapBlock::vertexCount()
{
	return m_vertexCount;
}

uint32_t DMClipMapBlock::indexCount()
{
	return m_indexCount;
}