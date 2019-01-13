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

	rawVertex.emplace_back( -5.0, -5.0, 0.0 );
	rawVertex.emplace_back( 5.0, -5.0, 0.0 );
	rawVertex.emplace_back( 5.0, 5.0, 0.0 );
	rawVertex.emplace_back( -5.0, 5.0, 0.0 );

	if( !DMD3D::instance().createVertexBuffer( m_vertexBuffer, &rawVertex[0], rawVertex.size() * sizeof( XMFLOAT3 ) ) )
	{
		LOG( "Can`t create vertex buffer" );
		return false;
	}
	
	const uint32_t indicesQuad[4] = { 0,1,2,3 };
	if( !DMD3D::instance().createIndexBuffer( m_indexBuffer, (void*)&indicesQuad[0], 4 * sizeof( uint32_t ) ) )
	{
		LOG( "Can`t create index buffer" );
		return false;
	}

	return true;
}

uint32_t Path::indexCount()
{
	return 4;
}

ID3D11Buffer* Path::vertexBuffer()
{
	return m_vertexBuffer.get();
}

ID3D11Buffer* Path::indexBuffer()
{
	return m_indexBuffer.get();
}