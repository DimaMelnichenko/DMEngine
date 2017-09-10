#include "DMQuadTree_.h"


DMQuadTree_::DMQuadTree_(  )
{
	m_vertex_buffer = NULL;
	m_index_buffer = NULL;
	m_patch_vetex_count = 256;
}


DMQuadTree_::~DMQuadTree_()
{
	Shutdown();
}

bool DMQuadTree_::Initialize( ID3D11Device* _device )
{
	bool result = true;

	result = InitializeBuffers( _device );
	if( !result )
		return false;

	result = InitNode( 0, 0, 0, 256 );
	if( !result )
		return false;

	return true;
}



bool DMQuadTree_::InitNode( Node* _parent, float _pos_x, float _pos_y, float _width )
{
	Node* node = new Node;

	if( !_parent )
	{
		m_main_node = node;
	}
	node->parent = ( _parent ? _parent : node );
	node->center = D3DXVECTOR3( _width / 2.0f, 0.0, _width / 2.0f );

	if( _width > 1.0f )
	{
		for( int i = 0; i < 4; i++ )
		{
			// Calculate the position offsets for the new child node.
			float offsetX = ( ( ( i % 2 ) < 1 ) ? -1.0f : 1.0f ) * ( _width / 4.0f );
			float offsetZ = ( ( ( i % 4 ) < 2 ) ? -1.0f : 1.0f ) * ( _width / 4.0f );			

			// Extend the tree starting from this new child node now.
			InitNode( node, ( _pos_x + offsetX ), ( _pos_y + offsetZ ), _width / 2.0f );

		}
	}

	return true;
}

bool DMQuadTree_::InitializeBuffers( ID3D11Device* _device )
{
	VertexType* vertices = 0;
	unsigned long* indices = 0;
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;

	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
//	vertexBufferDesc.ByteWidth = sizeof(VertexType) * m_vertexCount;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = 0;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	//result = m_dmd3d->GetDevice()->CreateBuffer( &vertexBufferDesc, &vertexData, &m_vertex_buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	//indexBufferDesc.ByteWidth = sizeof(unsigned long)* m_indexCount;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	//result = m_dmd3d->GetDevice()->CreateBuffer( &indexBufferDesc, &indexData, &m_indexBuffer );
	if( FAILED( result ) )
	{
		return false;
	}

	// Release the arrays now that the vertex and index buffers have been created and loaded.
	delete[] vertices;
	vertices = 0;

	delete[] indices;
	indices = 0;

	return true;
}

void DMQuadTree_::Shutdown()
{
	if( m_vertex_buffer )
	{
		m_vertex_buffer->Release();
		m_vertex_buffer = NULL;
	}

	if( m_index_buffer )
	{
		m_index_buffer->Release();
		m_index_buffer = NULL;
	}
}