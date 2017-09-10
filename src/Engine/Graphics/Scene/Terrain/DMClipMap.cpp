#include "DMClipMap.h"
#include <vector>

///////////////////////////////////////////
int grid = 64;				// patch resolution
int levels = 5;				// LOD levels
int width = 2048, height = 2048; // heightmap dimensions
///////////////////////////////////////////

DMClipMap::DMClipMap( DMObject* _parent ) : DMObject( _parent )
{
	m_vertexBuffer = nullptr;
	m_indexBuffer = nullptr; 
}


DMClipMap::~DMClipMap()
{
	Shutdown();
}


bool DMClipMap::Initialize( ID3D11Device* _device )
{
	// Initialize the vertex and index buffers.
	bool result = InitializeBuffers( _device );
	if( !result )
	{
		return false;
	}
}

void DMClipMap::Render( ID3D11DeviceContext* _deviceContext, D3DXMATRIX* matrixProjection )
{
	// Re-build the dynamic vertex buffer for rendering to possibly a different location on the screen.
	bool result = UpdateBuffers( _deviceContext );
	if( !result )
	{
		return;
	}

	// Put the vertex and index buffers on the graphics pipeline to prepare them for drawing.
	RenderBuffers( _deviceContext, matrixProjection );
}

void DMClipMap::Shutdown()
{
	ShutdownBuffers( );
}


bool DMClipMap::InitializeBuffers( ID3D11Device* device )
{
	std::vector<float> vert;

	for( size_t j = 0; j < grid + 1; ++j )
	{
		for( size_t i = 0; i < grid + 2; i++ )
		{
			for( size_t k = 0; k < ( ( i == 0 ) ? 2 : 1 ); k++ )
			{
				vert.push_back( float( i ) / grid );
				vert.push_back( float( j ) / grid );
				vert.push_back( 0 );
			}
			++j;
			for( size_t k = 0; k < ( ( i == grid + 1 ) ? 2 : 1 ); k++ )
			{
				vert.push_back( float( i ) / grid );
				vert.push_back( float( j ) / grid );
				vert.push_back( 0 );
			}
			--j;
		}
	}

	D3D11_BUFFER_DESC vertexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData;

	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(float)* vert.size();
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = &vert[0];
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	bool result = device->CreateBuffer( &vertexBufferDesc, &vertexData, &m_vertexBuffer );
	if( FAILED( result ) )
	{
		return false;
	}
}

void DMClipMap::ShutdownBuffers()
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

bool DMClipMap::UpdateBuffers( ID3D11DeviceContext* )
{
	return true;
}

void DMClipMap::RenderBuffers( ID3D11DeviceContext*, D3DXMATRIX* matrixProjection )
{
	float sxy = 2; // scale x/y
	//shader.setUniform4f( "map_position", -viewpos.x / float( 2 * 512 * grid ), -viewpos.z / float( 2 * 512 * grid ), 0, 0 );

	for( size_t i = 0; i < levels; i++ )
	{
	
		//float ox = ( int( viewpos.x * ( 1 << i ) ) & 511 ) / float( 512 * grid );
		//float oy = ( int( viewpos.z * ( 1 << i ) ) & 511 ) / float( 512 * grid );
		float ox = ( int( 1 * ( 1 << i ) ) & 511 ) / float( 512 * grid );
		float oy = ( int( 1 * ( 1 << i ) ) & 511 ) / float( 512 * grid );

		D3DXVECTOR3 scale( sxy * 0.25, sxy * 0.25, 1 );
		//shader.setUniform4f( "scale", scale.x, scale.y, 1, 1 );

		for( size_t k = -2; k < 2; k++ )
		{

			for( size_t j = -2; j < 2; j++ )
			{
				if( i != levels - 1 ) if( k == -1 || k == 0 ) if( j == -1 || j == 0 ) continue;

				D3DXVECTOR3 offset( ox + float( j ), oy + float( k ), 0 );
				if( k >= 0 ) offset.y -= 1.0 / float( grid ); // adjust offset for proper overlapping
				if( j >= 0 ) offset.x -= 1.0 / float( grid ); // adjust offset for proper overlapping
				/*
				//cull
				int xp = 0, xm = 0, yp = 0, ym = 0, zp = 0;
				for( size_t l = 0; l < 2; l++ )
				{
					for( size_t m = 0; m < 2; m++ )
					{
						for( size_t n = 0; n < 2; n++ )
						{
							D3DXVECTOR3 v;
							v = offset + D3DXVECTOR3( l, m, float( -n ) * 0.05 );
							D3DXVec3Cross( &v, &scale, &v );							   
							D3DXVE cs = matrixProjection * vec4f( v.x, v.y, v.z, 1 ); // clipspace
							if( cs.z < cs.w ) zp++;
							if( cs.x<-cs.w ) xm++;	if( cs.x>cs.w ) xp++;
							if( cs.y<-cs.w ) ym++;	if( cs.y>cs.w ) yp++;
						}
					}
				}				
				
				if( zp == 0 || xm == 8 || xp == 8 || ym == 8 || yp == 8 )continue; // skip if invisible
				*/
				//render
				//shader.setUniform4f( "offset", offset.x, offset.y, 0, 0 );
				/*
				if( wireframe )	glDrawArrays( GL_LINES, 0, vert.size() / 3 );
				else			glDrawArrays( GL_TRIANGLE_STRIP, 0, vert.size() / 3 );
				*/
			}
			sxy *= 0.5;
		}
	}
}