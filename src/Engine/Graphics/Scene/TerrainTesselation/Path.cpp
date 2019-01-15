#include "Path.h"
#include "D3D/DMD3D.h"
#include "Logger/Logger.h"


Path::Path()
{
}


Path::~Path()
{
}


bool Path::reinitializeBuffers( uint16_t pathSize )
{
	std::vector<XMFLOAT3> rawVertex;
	rawVertex.reserve( ( pathSize + 1 ) * ( pathSize + 1 ) );

	for( int32_t j = -pathSize / 2; j < pathSize / 2 + 1; ++j )
	{	
		for( int32_t i = -pathSize / 2; i < pathSize / 2 + 1; ++i )
		{
			rawVertex.emplace_back( (float)i, 0.0, (float)j );
		}
	}

	if( !DMD3D::instance().createVertexBuffer( m_vertexBuffer, &rawVertex[0], rawVertex.size() * sizeof( XMFLOAT3 ) ) )
	{
		LOG( "Can`t create vertex buffer" );
		return false;
	}
	
	std::vector<uint32_t> indicesQuad;
	indicesQuad.reserve( pathSize * pathSize * 4 );

	for( uint32_t j = 0; j < pathSize; ++j )
	{
		uint32_t rowOffset = j * ( pathSize + 1 );
		for( uint32_t i = 0; i < pathSize; ++i )
		{	
			indicesQuad.emplace_back( rowOffset + i );
			indicesQuad.emplace_back( rowOffset + i + 1 );
			indicesQuad.emplace_back( rowOffset + pathSize + i + 2 );
			indicesQuad.emplace_back( rowOffset + pathSize + i + 1 );
		}
	}

	m_indexCount = indicesQuad.size();

	if( !DMD3D::instance().createIndexBuffer( m_indexBuffer, (void*)&indicesQuad[0], indicesQuad.size() * sizeof( uint32_t ) ) )
	{
		LOG( "Can`t create index buffer" );
		return false;
	}

	return true;
}

uint32_t Path::indexCount()
{
	return m_indexCount;
}

ID3D11Buffer* Path::vertexBuffer()
{
	return m_vertexBuffer.get();
}

ID3D11Buffer* Path::indexBuffer()
{
	return m_indexBuffer.get();
}