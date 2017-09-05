#include "DMMesh.h"
#include <fstream>
#include "utilites.h"



DMMesh::DMMesh( DMD3D* parent ) : DMSceneObject( parent )
{
	m_indices = nullptr;
	m_vertices = nullptr;
	m_vertex_combination = V_PTNTB;
}


DMMesh::~DMMesh()
{
}

bool DMMesh::loadMesh( VertexCombination combination, WCHAR* file )
{
	m_vertex_combination = combination;

	return InitializeBuffers( file );
}

void DMMesh::Render()
{
	RenderBuffers();
}

bool DMMesh::InitializeBuffers( WCHAR* modelFilename )
{
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;

	// Load in the model data,
	if( !readFile( modelFilename ) )
	{
		return false;
	}

	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;

	switch( m_vertex_combination )
	{
		case V_PTN:
			vertexBufferDesc.ByteWidth = sizeof( VertexPTN )* m_vertex_count;
			aabb().CreateAABBFromVertex( (void*)m_vertices, sizeof( VertexPTN ), m_vertex_count );
			break;
		case V_PTNTB:
			vertexBufferDesc.ByteWidth = sizeof( VertexPTNTB )* m_vertex_count;
			aabb().CreateAABBFromVertex( (void*)m_vertices, sizeof( VertexPTNTB ), m_vertex_count );
			break;
	}

	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = 0;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = m_vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	ID3D11Buffer* buffer;
	result = m_dmd3d->GetDevice()->CreateBuffer( &vertexBufferDesc, &vertexData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_vertex_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof( unsigned long )* m_index_count;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = m_indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	result = m_dmd3d->GetDevice()->CreateBuffer( &indexBufferDesc, &indexData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_index_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	// Release the arrays now that the vertex and index buffers have been created and loaded.
	delete[] m_vertices;
	m_vertices = 0;

	delete[] m_indices;
	m_indices = 0;

	return true;
}

bool DMMesh::readFile( WCHAR* filename )
{
	std::ifstream fin;

	// Open the model file.
	fin.open( filename, std::fstream::in | std::fstream::binary );

	// If it could not open the file then exit.
	if( fin.fail() )
	{
		return false;
	}

	struct FileHeader
	{
		unsigned int vertex_count;
		unsigned int index_count;
	} file_header;

	struct Vertex
	{
		D3DXVECTOR3 pos;
		D3DXVECTOR3 norm;
		D3DXVECTOR2 texcoord;
		D3DXVECTOR3 tangent;
		D3DXVECTOR3 binorm;
	} file_vertex;

	fin.read( reinterpret_cast<char*>( &file_header ), sizeof( FileHeader ) );

	m_vertex_count = file_header.vertex_count;

	switch( m_vertex_combination )
	{
		case V_PTN:
			m_vertices = new char[m_vertex_count * sizeof( VertexPTN )];
			break;
		case V_PTNTB:
			m_vertices = new char[m_vertex_count * sizeof( VertexPTNTB )];
			break;
	}



	m_index_count = file_header.index_count;
	m_indices = new unsigned long[m_index_count];

	VertexPTN* vertex_PTN;
	VertexPTNTB* vertex_PTNTB;

	// Read in the vertex data.
	for( size_t i = 0; i < file_header.vertex_count; i++ )
	{
		fin.read( reinterpret_cast<char*>( &file_vertex ), sizeof( Vertex ) );

		switch( m_vertex_combination )
		{
			case V_PTN:
				vertex_PTN = reinterpret_cast<VertexPTN*>( m_vertices + ( sizeof( VertexPTN )* i ) );
				vertex_PTN->position = file_vertex.pos;
				vertex_PTN->normal = file_vertex.norm;
				vertex_PTN->texture = file_vertex.texcoord;
				break;
			case V_PTNTB:
				vertex_PTNTB = reinterpret_cast<VertexPTNTB*>( m_vertices + ( sizeof( VertexPTNTB )* i ) );
				vertex_PTNTB->position = file_vertex.pos;
				vertex_PTNTB->normal = file_vertex.norm;
				vertex_PTNTB->texture = file_vertex.texcoord;
				vertex_PTNTB->tangent = file_vertex.tangent;
				vertex_PTNTB->binormal = file_vertex.binorm;

				break;
		}
	}

	// Read in the index data.
	for( size_t i = 0; i < file_header.index_count; ++i )
	{
		unsigned long index;
		fin.read( reinterpret_cast<char*>( &index ), sizeof( unsigned long ) );
		m_indices[i] = index;
	}

	// Close the model file.
	fin.close();

	return true;
}

void DMMesh::RenderBuffers()
{
	unsigned int stride = 0;
	unsigned int offset = 0;


	// Set vertex buffer stride and offset.
	switch( m_vertex_combination )
	{
		case V_PTN:
			stride = sizeof( VertexPTN );
			break;
		case V_PTNTB:
			stride = sizeof( VertexPTNTB );
			break;
	}
	offset = 0;

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	ID3D11Buffer* buffer = m_vertex_buffer.get();
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );

	// Set the index buffer to active in the input assembler so it can be rendered.
	buffer = m_index_buffer.get();
	m_dmd3d->GetDeviceContext()->IASetIndexBuffer( buffer, DXGI_FORMAT_R32_UINT, 0 );

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	m_dmd3d->GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	return;
}

int DMMesh::vertexSize()
{
	switch( m_vertex_combination )
	{
		case V_PTN:
			return sizeof( VertexPTN );
			break;
		case V_PTNTB:
			return sizeof( VertexPTNTB );
			break;
	}

	return 0;
}

ID3D11Buffer* DMMesh::vertexBuffer()
{
	return m_vertex_buffer.get();
}

unsigned long DMMesh::GetIndexCount()
{
	return m_index_count;
}