#include "DMQuadTree.h"
#include <assert.h>

DMQuadTree::DMQuadTree()
{
	
}

DMQuadTree::~DMQuadTree()
{

}

bool DMQuadTree::Initialize( float side_length, float min_length )
{	
	m_side_length = side_length;
	m_min_length = min_length;

	m_main_node = std::unique_ptr<NodeType>( new NodeType );
	m_main_node->child.reserve( 4 );
	m_main_node->parent = nullptr;
	m_main_node->size = side_length;	
	D3DXVECTOR3 pos( side_length / 2.0f, 50.0f, side_length / 2.0f );
	D3DXVECTOR3 size( side_length / 2.0f, 50.0f, side_length / 2.0f );	
	m_main_node->aabb.CreateAABB( pos, size );
	

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// initialize verex buffer
	//
	/////////////////////////
	

	D3D11_BUFFER_DESC vertex_buffer_desc, index_buffer_desc;
	D3D11_SUBRESOURCE_DATA vertex_data, index_data;

	vertex_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	vertex_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertex_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertex_buffer_desc.MiscFlags = 0;
	vertex_buffer_desc.StructureByteStride = 0;
	vertex_buffer_desc.ByteWidth = sizeof( Vertex )* m_vertex_count;
	
	Vertex vertex[8];

	vertex[0].pos = D3DXVECTOR3( -0.5f, -0.5f, -0.5f );
	vertex[1].pos = D3DXVECTOR3( -0.5f,  0.5f, -0.5f );
	vertex[2].pos = D3DXVECTOR3(  0.5f,  0.5f, -0.5f );
	vertex[3].pos = D3DXVECTOR3(  0.5f, -0.5f, -0.5f );
	vertex[4].pos = D3DXVECTOR3( -0.5f, -0.5f,  0.5f );
	vertex[5].pos = D3DXVECTOR3( -0.5f,  0.5f,  0.5f );
	vertex[6].pos = D3DXVECTOR3(  0.5f,  0.5f,  0.5f );
	vertex[7].pos = D3DXVECTOR3(  0.5f, -0.5f,  0.5f );

	vertex[0].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );
	vertex[1].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );
	vertex[2].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );
	vertex[3].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );
	vertex[4].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );
	vertex[5].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );
	vertex[6].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );
	vertex[7].color = D3DXVECTOR3( 1.0, 0.0f, 0.0f );

	vertex_data.pSysMem = vertex;
	vertex_data.SysMemPitch = 0;
	vertex_data.SysMemSlicePitch = 0;

	

	// Now create the vertex buffer.
	DMD3D::instance().CreateBuffer( &vertex_buffer_desc, &vertex_data, m_vertex_buffer );
	//ID3D11Buffer* buffer;
	//HRESULT result = DMD3D::instance().GetDevice()->CreateBuffer( &vertex_buffer_desc, &vertex_data, &buffer );
	//if( FAILED( result ) )
	//{
	//	return false;
	//}
	//
	//m_vertex_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	// Set up the description of the static index buffer.
	index_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	index_buffer_desc.ByteWidth = sizeof( unsigned long )* m_index_count;
	index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	index_buffer_desc.CPUAccessFlags = 0;
	index_buffer_desc.MiscFlags = 0;
	index_buffer_desc.StructureByteStride = 0;

	unsigned long indices[24];

	// front
	indices[0] = 0; indices[1] = 1;
	indices[2] = 1; indices[3] = 2;
	indices[4] = 2; indices[5] = 3;
	indices[6] = 3; indices[7] = 0;
	//back
	indices[8] = 4; indices[9] = 5;
	indices[10] = 5; indices[11] = 6;
	indices[12] = 6; indices[13] = 7;
	indices[14] = 7; indices[15] = 4;
	//connect
	indices[16] = 0; indices[17] = 4;
	indices[18] = 1; indices[19] = 5;
	indices[20] = 2; indices[21] = 6;
	indices[22] = 3; indices[23] = 7;
	
	index_data.pSysMem = indices;
	index_data.SysMemPitch = 0;
	index_data.SysMemSlicePitch = 0;

	DMD3D::instance().CreateBuffer( &index_buffer_desc, &index_data, m_index_buffer );

	// Create the index buffer.
	//HRESULT result = DMD3D::instance().GetDevice()->CreateBuffer( &index_buffer_desc, &index_data, &buffer );
	//if( FAILED( result ) )
	//{
	//	return false;
	//}
	//
	//m_index_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	return true;
}

void DMQuadTree::addNodes( NodeType* parent_node, DMSceneObject* object )
{
	if( parent_node->size / 2.0 < m_min_length )
	{
		if( parent_node->aabb.OverlapsAABB( object->aabb() ) )
		{
			//object->setFrustum( false );
			parent_node->objects.push_back( object );
		}
		return;
	}

	D3DXVECTOR3 object_pos = object->aabb().position();
	D3DXVECTOR3 node_pos = parent_node->aabb.position();
	
	float half_length = parent_node->size;

	if( parent_node->child.size() == 0 )
	{
		for( int i = 0; i < 4; i++ )
		{
			// Calculate the position offsets for the new child node.
			float offsetX = ( ( ( i % 2 ) < 1 ) ? -1.0f : 1.0f ) * ( parent_node->size / 4.0f );
			float offsetZ = ( ( ( i % 4 ) < 2 ) ? -1.0f : 1.0f ) * ( parent_node->size / 4.0f );

			NodeType* child_node = new NodeType;
			child_node->parent = parent_node;
			child_node->size = parent_node->size / 2.0f;
			child_node->child.reserve( 4 );
			D3DXVECTOR3 pos( parent_node->aabb.position().x + offsetX, 25.0f, parent_node->aabb.position().z + offsetZ );
			D3DXVECTOR3 size( child_node->size / 2.0f, 25.0f, child_node->size / 2.0f );
			child_node->aabb.CreateAABB( pos, size );

			parent_node->child.push_back( std::move( std::unique_ptr<NodeType>( child_node ) ) );
		}
	}

	if( object_pos.x > node_pos.x )
	{
		if( object_pos.z > node_pos.z )
		{
			addNodes( parent_node->child[3].get(), object );
		}
		else
		{
			addNodes( parent_node->child[1].get(), object );
		}
	}
	else
	{
		if( object_pos.z > node_pos.z )
		{
			addNodes( parent_node->child[2].get(), object );
		}
		else
		{
			addNodes( parent_node->child[0].get(), object );
		}
	}

}

void DMQuadTree::checkObjects( const DMFrustum& frustum, const DMCamera& camera )
{
	m_visible_objects.clear();

	checkNode( m_main_node.get(), frustum, camera );
}

void DMQuadTree::checkNode( NodeType* node, const DMFrustum& frustum, const DMCamera& camera )
{
	if( frustum.CheckAABB( &node->aabb ) )
	{	
		for( size_t i = 0; i < node->child.size(); i++ )
		{
			checkNode( node->child[i].get(), frustum, camera );
		}		
		
		for( size_t j = 0; j < node->objects.size(); ++j )
		{
			if( frustum.CheckAABB( &( node->objects[j]->aabb() ) ) )
			{
				node->objects[j]->setInFrustum( camera );
			}
			//m_visible_objects.push_back( node->objects[j].get() );
		}
	}
	else
	{
		//hideNode( node );		
	}
}

void DMQuadTree::addObject( DMSceneObject* object )
{
	addNodes( m_main_node.get(), object );
}

void DMQuadTree::setDrawBuffers()
{
	unsigned int offset = 0;
	unsigned int stride = sizeof( Vertex );

	ID3D11Buffer* buffer = m_vertex_buffer.get();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );

	// Set the index buffer to active in the input assembler so it can be rendered.
	//buffer = m_index_buffer.get();
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_index_buffer.get(), DXGI_FORMAT_R32_UINT, 0 );

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_LINELIST );

}

void DMQuadTree::Render( DMColorShader* shader, const DMCamera& camera )
{
	drawNode( m_main_node.get(), shader, camera );
}

void DMQuadTree::drawNode( NodeType* node, DMColorShader* shader, const DMCamera& camera )
{
	for( size_t i = 0; i < 4; i++ )
	{
		if( node->child.size() )
			drawNode( node->child[i].get(), shader, camera );
		else
			break;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( m_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	static Vertex* vertex;

	// Get a pointer to the data in the constant buffer.
	vertex = (Vertex*)mappedResource.pData;

	D3DXVECTOR3 pos = node->aabb.position();
	D3DXVECTOR3 size = node->aabb.size();

	vertex[0].pos = pos - size;
	vertex[0].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );
	vertex[1].pos = pos - size;
	vertex[1].pos.y = pos.y + size.y;
	vertex[1].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );
	vertex[2].pos = pos - size;
	vertex[2].pos.x = pos.x + size.x;
	vertex[2].pos.y = pos.y + size.y;
	vertex[2].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );
	vertex[3].pos = pos - size;
	vertex[3].pos.x = pos.x + size.x;
	vertex[3].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );

	vertex[4].pos = pos - size;
	vertex[4].pos.z = pos.z + size.z;
	vertex[4].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );
	vertex[5].pos = pos - size;
	vertex[5].pos.z = pos.z + size.z;
	vertex[5].pos.y = pos.y + size.y;
	vertex[5].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );
	vertex[6].pos = pos + size;
	vertex[6].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );
	vertex[7].pos = pos + size;
	vertex[7].pos.y = pos.y - size.y;
	vertex[7].color = D3DXVECTOR3( 1.0, 0.0, 0.0 );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( m_vertex_buffer.get(), 0 );
	
	//setDrawBuffers();

	D3DXMATRIX mat;

	D3DXMatrixIdentity( &mat );

	//shader->Prepare();
	shader->Render( m_index_count, &mat );
	 
	
}

float DMQuadTree::sideLength()
{
	return m_side_length;
}

float DMQuadTree::minLength()
{
	return m_min_length;
}