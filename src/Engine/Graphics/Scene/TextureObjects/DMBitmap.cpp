#include "DMBitmap.h"


DMBitmap::DMBitmap()
{	
	
}

DMBitmap::~DMBitmap()
{
	
}

bool DMBitmap::Initialize( int screenWidth, int screenHeight, int bitmapWidth, int bitmapHeight )
{
	bool result;

	// Store the screen size.
 	m_screenWidth = screenWidth;
	m_screenHeight = screenHeight;

	// Store the size in pixels that this bitmap should be rendered at.
	m_bitmapWidth = bitmapWidth;
	m_bitmapHeight = bitmapHeight;

	// Initialize the previous rendering position to negative one.
	m_previousPosX = -1;
	m_previousPosY = -1;

	// Initialize the vertex and index buffers.
	result = InitializeBuffers();
	if( !result )
	{
		return false;
	}

	return true;
}

bool DMBitmap::Render( int positionX, int positionY )
{
	bool result;


	// Re-build the dynamic vertex buffer for rendering to possibly a different location on the screen.
	result = UpdateBuffers( positionX, positionY );
	if( !result )
	{
		return false;
	}

	// Put the vertex and index buffers on the graphics pipeline to prepare them for drawing.
	RenderBuffers();

	return true;
}

int DMBitmap::GetIndexCount( )
{
	return m_indexCount;
}

bool DMBitmap::InitializeBuffers()
{
	VertexType* vertices;
	unsigned long* indices;
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;
	int i;

	// Set the number of vertices in the vertex array.
	m_vertexCount = 6;

	// Set the number of indices in the index array.
	m_indexCount = m_vertexCount;

	// Create the vertex array.
	vertices = new VertexType[m_vertexCount];
	if( !vertices )
	{
		return false;
	}

	// Create the index array.
	indices = new unsigned long[m_indexCount];
	if( !indices )
	{
		return false;
	}

	// Initialize vertex array to zeros at first.
	memset( vertices, 0, ( sizeof(VertexType)* m_vertexCount ) );

	// Load the index array with data.
	for( i = 0; i<m_indexCount; i++ )
	{
		indices[i] = i;
	}

	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(VertexType)* m_vertexCount;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	ID3D11Buffer* buffer;
	result = DMD3D::instance().GetDevice()->CreateBuffer( &vertexBufferDesc, &vertexData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_vertexBuffer = make_com_ptr<ID3D11Buffer>( buffer );

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(unsigned long)* m_indexCount;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	result = DMD3D::instance().GetDevice()->CreateBuffer( &indexBufferDesc, &indexData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_indexBuffer = make_com_ptr<ID3D11Buffer>( buffer );

	// Release the arrays now that the vertex and index buffers have been created and loaded.
	delete[] vertices;
	vertices = 0;

	delete[] indices;
	indices = 0;

	return true;
}

bool DMBitmap::UpdateBuffers( int positionX, int positionY )
{
	float left, right, top, bottom;
	VertexType* vertices;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	VertexType* verticesPtr;
	HRESULT result;

	// If the position we are rendering this bitmap to has not changed then don't update the vertex buffer since it
	// currently has the correct parameters.
	if( ( positionX == m_previousPosX ) && ( positionY == m_previousPosY ) )
	{
		return true;
	}

	// If it has changed then update the position it is being rendered to.
	m_previousPosX = positionX;
	m_previousPosY = positionY;

	// Calculate the screen coordinates of the left side of the bitmap.
	left = (float)( ( m_screenWidth / 2 ) * -1 ) + (float)positionX;

	// Calculate the screen coordinates of the right side of the bitmap.
	right = left + (float)m_bitmapWidth;

	// Calculate the screen coordinates of the top of the bitmap.
	top = (float)( m_screenHeight / 2 ) - (float)positionY;

	// Calculate the screen coordinates of the bottom of the bitmap.
	bottom = top - (float)m_bitmapHeight;

	// Create the vertex array.
	vertices = new VertexType[m_vertexCount];
	if( !vertices )
	{
		return false;
	}

	// Load the vertex array with data.
	// First triangle.
	vertices[0].position = XMFLOAT3( left, top, 0.0f );  // Top left.
	vertices[0].texture = XMFLOAT2( 0.0f, 0.0f );

	vertices[1].position = XMFLOAT3( right, bottom, 0.0f );  // Bottom right.
	vertices[1].texture = XMFLOAT2( 1.0f, 1.0f );

	vertices[2].position = XMFLOAT3( left, bottom, 0.0f );  // Bottom left.
	vertices[2].texture = XMFLOAT2( 0.0f, 1.0f );

	// Second triangle.
	vertices[3].position = XMFLOAT3( left, top, 0.0f );  // Top left.
	vertices[3].texture = XMFLOAT2( 0.0f, 0.0f );

	vertices[4].position = XMFLOAT3( right, top, 0.0f );  // Top right.
	vertices[4].texture = XMFLOAT2( 1.0f, 0.0f );

	vertices[5].position = XMFLOAT3( right, bottom, 0.0f );  // Bottom right.
	vertices[5].texture = XMFLOAT2( 1.0f, 1.0f );

	// Lock the vertex buffer so it can be written to.
	result = DMD3D::instance().GetDeviceContext()->Map( m_vertexBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the vertex buffer.
	verticesPtr = (VertexType*)mappedResource.pData;

	// Copy the data into the vertex buffer.
	memcpy( verticesPtr, (void*)vertices, ( sizeof(VertexType)* m_vertexCount ) );

	// Unlock the vertex buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( m_vertexBuffer.get(), 0 );

	// Release the vertex array as it is no longer needed.
	delete[] vertices;
	vertices = 0;

	return true;
}

void DMBitmap::RenderBuffers()
{
	unsigned int stride;
	unsigned int offset;


	// Set vertex buffer stride and offset.
	stride = sizeof( VertexType );
	offset = 0;

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	ID3D11Buffer* buffer = m_vertexBuffer.get();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );

	// Set the index buffer to active in the input assembler so it can be rendered.
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_indexBuffer.get(), DXGI_FORMAT_R32_UINT, 0 );

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	return;
}