#include "DMWater.h"


DMWater::DMWater( DMD3D* parent ) : DMSceneObject( parent )
{
	m_vertexBuffer = 0;
	m_indexBuffer = 0;
	m_Texture = 0;
}

DMWater::~DMWater()
{
	Shutdown();
}

bool DMWater::Initialize( WCHAR* textureFilename, float waterHeight, float waterRadius )
{
	bool result;

	// Store the water height.
	m_waterHeight = waterHeight;

	// Initialize the vertex and index buffer that hold the geometry for the triangle.
	result = InitializeBuffers( waterRadius );
	if( !result )
	{
		return false;
	}

	// Load the texture for this model.
	result = LoadTexture( textureFilename );
	if( !result )
	{
		return false;
	}

	// Set the tiling for the water normal maps.
	m_normalMapTiling.x = 0.01f;  // Tile ten times over the quad.
	m_normalMapTiling.y = 0.02f;  // Tile five times over the quad.

	// Initialize the water translation to zero.
	m_waterTranslation = 0.0f;

	// Set the scaling value for the water normal map.
	m_reflectRefractScale = 0.03f;

	// Set the tint of the refraction.
	m_refractionTint = D3DXVECTOR4( 0.0f, 0.8f, 1.0f, 1.0f );

	// Set the specular shininess.
	m_specularShininess = 200.0f;

	return true;
}

void DMWater::Shutdown( )
{
	// Release the model texture.
	ReleaseTexture( );

	// Release the vertex and index buffers.
	ShutdownBuffers( );

	return;
}

void DMWater::Frame( )
{
	// Update the position of the water to simulate motion.
	m_waterTranslation += 0.003f;
	if( m_waterTranslation > 1.0f )
	{
		m_waterTranslation -= 1.0f;
	}

	return;
}

void DMWater::Render()
{
	// Put the vertex and index buffers on the graphics pipeline to prepare them for drawing.
	RenderBuffers();

	return;
}

int DMWater::GetIndexCount( )
{
	return m_indexCount;
}

com_unique_ptr<ID3D11ShaderResourceView>& DMWater::GetTexture( )
{
	return m_Texture->GetTexture( );
}

float DMWater::GetWaterHeight( )
{
	return m_waterHeight;
}

D3DXVECTOR2 DMWater::GetNormalMapTiling( )
{
	return m_normalMapTiling;
}

float DMWater::GetWaterTranslation( )
{
	return m_waterTranslation;
}

float DMWater::GetReflectRefractScale( )
{
	return m_reflectRefractScale;
}

D3DXVECTOR4 DMWater::GetRefractionTint( )
{
	return m_refractionTint;
}

float DMWater::GetSpecularShininess( )
{
	return m_specularShininess;
}

bool DMWater::InitializeBuffers( float waterRadius )
{
	VertexType* vertices;
	unsigned long* indices;
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;


	// Set the number of vertices in the vertex array.
	m_vertexCount = 6;

	// Set the number of indices in the index array.
	m_indexCount = 6;

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

	// Load the vertex array with data.
	vertices[0].position = D3DXVECTOR3( -waterRadius, 0.0f, waterRadius );  // Top left.
	vertices[0].texture = D3DXVECTOR2( 0.0f, 0.0f );

	vertices[1].position = D3DXVECTOR3( waterRadius, 0.0f, waterRadius );  // Top right.
	vertices[1].texture = D3DXVECTOR2( 1.0f, 0.0f );

	vertices[2].position = D3DXVECTOR3( -waterRadius, 0.0f, -waterRadius );  // Bottom left.
	vertices[2].texture = D3DXVECTOR2( 0.0f, 1.0f );

	vertices[3].position = D3DXVECTOR3( -waterRadius, 0.0f, -waterRadius );  // Bottom left.
	vertices[3].texture = D3DXVECTOR2( 0.0f, 1.0f );

	vertices[4].position = D3DXVECTOR3( waterRadius, 0.0f, waterRadius );  // Top right.
	vertices[4].texture = D3DXVECTOR2( 1.0f, 0.0f );

	vertices[5].position = D3DXVECTOR3( waterRadius, 0.0f, -waterRadius );  // Bottom right.
	vertices[5].texture = D3DXVECTOR2( 1.0f, 1.0f );

	// Load the index array with data.
	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 3;
	indices[4] = 4;
	indices[5] = 5;

	// Set up the description of the vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vertexBufferDesc.ByteWidth = sizeof(VertexType)* m_vertexCount;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = 0;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now finally create the vertex buffer.
	ID3D11Buffer* vertex_buffer;	
	result = m_dmd3d->GetDevice()->CreateBuffer( &vertexBufferDesc, &vertexData, &vertex_buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_vertexBuffer = make_com_ptr<ID3D11Buffer>( vertex_buffer );

	// Set up the description of the index buffer.
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
	ID3D11Buffer* index_buffer;
	result = m_dmd3d->GetDevice()->CreateBuffer( &indexBufferDesc, &indexData, &index_buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	m_indexBuffer = make_com_ptr<ID3D11Buffer>( index_buffer );

	// Release the arrays now that the vertex and index buffers have been created and loaded.
	delete[] vertices;
	vertices = 0;

	delete[] indices;
	indices = 0;

	return true;
}

void DMWater::ShutdownBuffers( )
{
	// Release the index buffer.
	if( m_indexBuffer )
	{
		m_indexBuffer->Release( );
		m_indexBuffer = 0;
	}

	// Release the vertex buffer.
	if( m_vertexBuffer )
	{
		m_vertexBuffer->Release( );
		m_vertexBuffer = 0;
	}

	return;
}

void DMWater::RenderBuffers()
{
	unsigned int stride;
	unsigned int offset;


	// Set vertex buffer stride and offset.
	stride = sizeof( VertexType );
	offset = 0;

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	ID3D11Buffer* buffer = m_vertexBuffer.get();
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );

	// Set the index buffer to active in the input assembler so it can be rendered.
	m_dmd3d->GetDeviceContext()->IASetIndexBuffer( m_indexBuffer.get(), DXGI_FORMAT_R32_UINT, 0 );

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	m_dmd3d->GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	return;
}

bool DMWater::LoadTexture( WCHAR* filename )
{
	bool result;


	// Create the texture object.
	m_Texture =  std::unique_ptr<DMTexture>( new DMTexture( m_dmd3d ) );
	if( !m_Texture )
	{
		return false;
	}

	// Initialize the texture object.
	result = m_Texture->Initialize( filename );
	if( !result )
	{
		return false;
	}

	return true;
}

void DMWater::ReleaseTexture( )
{
	

	return;
}