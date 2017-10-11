#include "DMText.h"
#include <stdio.h>


DMText::DMText()
{
	
}


DMText::~DMText()
{

}

bool DMText::Initialize( unsigned int screenWidth, unsigned int screenHeight )
{
	bool result;

	// Store the screen width and height.
	m_screenWidth = screenWidth;
	m_screenHeight = screenHeight;

	// Initialize the font object.
	result = m_Font.Initialize( "src\\Engine\\Graphics\\Text\\fontdata.txt", L"Textures\\font.dds" );
	if( !result )
	{
		MessageBox( 0, L"Could not initialize the font object.", L"Error", MB_OK );
		return false;
	}

	// Initialize the font shader object.
	m_FontShader.Initialize( L"Shaders\\font.vs", L"Shaders\\font.ps" );
	m_FontShader.createPhase( 0, -1, 0 );
	if( !result )
	{
		MessageBox( 0, L"Could not initialize the font shader object.", L"Error", MB_OK );
		return false;
	}

	for( size_t i = 0; i < m_sentence_count; i++ )
	{
		SentenceType sentence;
		// Initialize the first sentence.
		result = InitializeSentence( sentence, 150 );
		if( !result )
		{
			return false;
		}

		m_sentences.push_back( std::move( sentence ) );
	}
	return true;
}

bool DMText::Render( D3DXMATRIX worldMatrix, const DMCamera& camera )
{
	bool result;

	m_FontShader.Prepare( camera, 0 );
	m_FontShader.setDrawType( DMShader::by_index );

	m_FontShader.setTexure( m_Font.GetTexture() );

	m_FontShader.setColor( D3DXVECTOR4( 0.3f, 1.0f, 0.3f, 1.0f ) );

	for( auto& sentence: m_sentences )
	{
		// Draw the first sentence.
		result = RenderSentence( sentence, camera );
		if( !result )
		{
			return false;
		}
	}


	return true;
}

bool DMText::InitializeSentence( SentenceType& sentence, unsigned int maxLength )
{	
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;

	// Set the maximum length of the sentence.
	sentence.max_length = maxLength;

	// Create the vertex array.
	sentence.raw_vertices.resize( 6 * maxLength );

	// Create the index array.
	sentence.raw_inices.resize( sentence.raw_vertices.size() );

	// Initialize the index array.
	for( size_t i = 0; i < sentence.raw_inices.size(); i++ )
	{
		sentence.raw_inices[i] = i;
	}

	// Set up the description of the dynamic vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof( DMFont::VertexType ) * sentence.raw_vertices.size();
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = &sentence.raw_vertices[0];
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Create the vertex buffer.
	ID3D11Buffer* buffer;
	result = DMD3D::instance().GetDevice()->CreateBuffer( &vertexBufferDesc, &vertexData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	sentence.vertexBuffer = make_com_ptr<ID3D11Buffer>( buffer );

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(unsigned long)* sentence.raw_vertices.size();
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = &sentence.raw_inices[0];
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	result = DMD3D::instance().GetDevice()->CreateBuffer( &indexBufferDesc, &indexData, &buffer );
	if( FAILED( result ) )
	{
		return false;
	}

	sentence.indexBuffer = make_com_ptr<ID3D11Buffer>( buffer );

	return true;
}

bool DMText::UpdateSentence( SentenceType& sentence, char* text, int positionX, int positionY, float red, float green, float blue )
{
	unsigned int numLetters;
	float drawX, drawY;
	HRESULT result;
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	// Store the color of the sentence.
	sentence.red = red;
	sentence.green = green;
	sentence.blue = blue;

	// Get the number of letters in the sentence.
	numLetters = (int)strlen( text );

	// Check for possible buffer overflow.
	if( numLetters > sentence.max_length )
	{
		return false;
	}

	// Calculate the X and Y pixel position on the screen to start drawing to.
	drawX = static_cast<float>( m_screenWidth )	 / 2.0f * -1 + positionX;
	drawY = static_cast<float>( m_screenHeight ) / 2.0f - positionY;


	// Use the font class to build the vertex array from the sentence text and sentence draw location.
	m_Font.BuildVertexArray( sentence.raw_vertices, text, drawX, drawY );

	// Lock the vertex buffer so it can be written to.
	result = DMD3D::instance().GetDeviceContext()->Map( sentence.vertexBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return false;
	}

	// Get a pointer to the data in the vertex buffer.
	DMFont::VertexType* verticesPtr = reinterpret_cast<DMFont::VertexType*>( mappedResource.pData );

	// Copy the data into the vertex buffer.
	memcpy( verticesPtr, &sentence.raw_vertices[0], ( sizeof(DMFont::VertexType) * sentence.raw_vertices.size() ) );

	// Unlock the vertex buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( sentence.vertexBuffer.get(), 0 );
	

	return true;
}

bool DMText::RenderSentence( const SentenceType& sentence, const DMCamera& camera )
{
	unsigned int stride, offset;
	D3DXVECTOR4 pixelColor;
	bool result;


	// Set vertex buffer stride and offset.
	stride = sizeof( DMFont::VertexType );
	offset = 0;

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	ID3D11Buffer* buffer = sentence.vertexBuffer.get();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );

	// Set the index buffer to active in the input assembler so it can be rendered.
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( sentence.indexBuffer.get(), DXGI_FORMAT_R32_UINT, 0 );

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	// Render the text using the font shader.
	result = m_FontShader.Render( sentence.raw_inices.size(), nullptr );
	if( !result )
	{
		false;
	}

	return true;
}

bool DMText::SetFps( unsigned int fps )
{
	char tempString[16];
	char fpsString[16];
	float red, green, blue;
	bool result;


	// Truncate the fps to below 10,000.
	if( fps > 9999 )
	{
		fps = 9999;
	}

	// Convert the fps integer to string format.
	_itoa_s( fps, tempString, 10 );

	// Setup the fps string.
	strcpy_s( fpsString, "Fps: " );
	strcat_s( fpsString, tempString );

	// If fps is 60 or above set the fps color to green.
	if( fps >= 60 )
	{
		red = 0.0f;
		green = 1.0f;
		blue = 0.0f;
	}

	// If fps is below 60 set the fps color to yellow.
	if( fps < 60 )
	{
		red = 1.0f;
		green = 1.0f;
		blue = 0.0f;
	}

	// If fps is below 30 set the fps color to red.
	if( fps < 30 )
	{
		red = 1.0f;
		green = 0.0f;
		blue = 0.0f;
	}

	// Update the sentence vertex buffer with the new string information.
	result = UpdateSentence( m_sentences[m_idx_fps], fpsString, 20, 20, red, green, blue );
	if( !result )
	{
		return false;
	}

	return true;
}

bool DMText::SetCpu( unsigned int cpu )
{	
	char cpuString[50];
	bool result;

	sprintf_s( cpuString, "CPU: %d%%", cpu );

	// Update the sentence vertex buffer with the new string information.
	result = UpdateSentence( m_sentences[m_idx_cpu], cpuString, 20, 40, 0.0f, 1.0f, 0.0f );
	if( !result )
	{
		return false;
	}

	return true;
}

bool DMText::SetVideoCardInfo( char* videoCardName, unsigned int videoCardMemory )
{
	char dataString[150];
	bool result;
	

	// Truncate the memory value to prevent buffer over flow.
	if( videoCardMemory > 9999999 )
	{
		videoCardMemory = 9999999;
	}

	sprintf_s( dataString, "Video Card: %s, Video Memory: %dMB", videoCardName, videoCardMemory );

	// Update the sentence vertex buffer with the new string information.
	result = UpdateSentence( m_sentences[m_idx_video], dataString, 80, 20, 1.0f, 1.0f, 1.0f );
	if( !result )
	{
		return false;
	}

	return true;

}

bool DMText::SetCameraPosition( float posX, float posY, float posZ )
{
	int positionX, positionY, positionZ;
	char dataString[50];
	bool result;


	// Convert the position from floating point to integer.
	positionX = (int)posX;
	positionY = (int)posY;
	positionZ = (int)posZ;

	// Truncate the position if it exceeds either 9999 or -9999.
	if( positionX > 9999 )
	{
		positionX = 9999;
	}
	if( positionY > 9999 )
	{
		positionY = 9999;
	}
	if( positionZ > 9999 )
	{
		positionZ = 9999;
	}

	if( positionX < -9999 )
	{
		positionX = -9999;
	}
	if( positionY < -9999 )
	{
		positionY = -9999;
	}
	if( positionZ < -9999 )
	{
		positionZ = -9999;
	}

	// Setup the X position string.
	sprintf_s( dataString, "X: %d, Y: %d, Z: %d", positionX, positionY, positionZ );	

	result = UpdateSentence( m_sentences[m_idx_camera_pos], dataString, 10, 130, 0.0f, 1.0f, 0.0f );
	if( !result )
	{
		return false;
	}

	return true;

}

bool DMText::SetCameraRotation( float rotX, float rotY, float rotZ )
{
	int rotationX, rotationY, rotationZ;
	char dataString[50];
	bool result;


	// Convert the rotation from floating point to integer.
	rotationX = (int)rotX;
	rotationY = (int)rotY;
	rotationZ = (int)rotZ;

	sprintf_s( dataString, "rX: %d, rY: %d, rZ: %d", rotationX, rotationY, rotationZ );

	result = UpdateSentence( m_sentences[m_idx_camera_rot], dataString, 10, 210, 0.0f, 1.0f, 0.0f );
	if( !result )
	{
		return false;
	}

	return true;
}

bool DMText::SetRenderCount( unsigned int count )
{
	
	char renderString[32];
	bool result;


	// Truncate the render count if it gets to large to prevent a buffer overflow.
	if( count > 999999999 )
	{
		count = 999999999;
	}

	sprintf_s( renderString, "Model Draw Count: %d", count );

	// Update the sentence vertex buffer with the new string information.
	result = UpdateSentence( m_sentences[m_idx_vertex_num], renderString, 10, 290, 0.0f, 1.0f, 0.0f );
	if( !result )
	{
		return false;
	}

	return true;
}