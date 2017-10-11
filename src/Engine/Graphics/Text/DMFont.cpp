#include "DMFont.h"


DMFont::DMFont()
{
}


DMFont::~DMFont()
{

}

bool DMFont::Initialize( char* fontFilename, WCHAR* textureFilename )
{
	bool result;


	// Load in the text file containing the font data.
	result = LoadFontData( fontFilename );
	if( !result )
	{
		return false;
	}

	// Load the texture that has the font characters on it.
	result = LoadTexture( textureFilename );
	if( !result )
	{
		return false;
	}

	return true;
}


bool DMFont::LoadFontData( char* filename )
{
	std::ifstream fin;
	int i;
	char temp;

	// Read in the font size and spacing between chars.
	fin.open( filename );
	if( fin.fail( ) )
	{
		return false;
	}

	// Read in the 95 used ascii characters for text.
	for( i = 0; i < 95; i++ )
	{
		fin.get( temp );
		while( temp != ' ' )
		{
			fin.get( temp );
		}
		fin.get( temp );
		while( temp != ' ' )
		{
			fin.get( temp );
		}

		FontType font_type;

		fin >> font_type.left;
		fin >> font_type.right;
		fin >> font_type.size;

		m_Font.push_back( font_type );
	}

	// Close the file.
	fin.close( );

	return true;
}

bool DMFont::LoadTexture( WCHAR* filename )
{
	bool result;

	// Initialize the texture object.
	result = m_Texture.Initialize( filename );
	if( !result )
	{
		return false;
	}

	return true;
}

ID3D11ShaderResourceView* DMFont::GetTexture( )
{
	return m_Texture.GetTexture( );
}

void DMFont::BuildVertexArray( std::vector<VertexType>& vertices, char* sentence, float drawX, float drawY )
{
	VertexType vertex_value;
	unsigned int numLetters, letter;

	// Get the number of letters in the sentence.
	numLetters = (int)strlen( sentence );

	unsigned int index = 0;

	// Draw each letter onto a quad.
	for( unsigned int i = 0; i < numLetters; i++ )
	{
		letter = ( (int)sentence[i] ) - 32;

		// If the letter is a space then just move over three pixels.
		if( letter == 0 )
		{
			drawX = drawX + 3.0f;
		}
		else
		{
			// First triangle in quad.
			vertex_value.position = D3DXVECTOR3( drawX, drawY, 0.0f );  // Top left.
			vertex_value.texture = D3DXVECTOR2( m_Font[letter].left, 0.0f );
			vertices[index] = vertex_value;
			index++;
			

			vertex_value.position = D3DXVECTOR3( ( drawX + m_Font[letter].size ), ( drawY - 16 ), 0.0f );  // Bottom right.
			vertex_value.texture = D3DXVECTOR2( m_Font[letter].right, 1.0f );
			vertices[index] = vertex_value;
			index++;
			

			vertex_value.position = D3DXVECTOR3( drawX, ( drawY - 16 ), 0.0f );  // Bottom left.
			vertex_value.texture = D3DXVECTOR2( m_Font[letter].left, 1.0f );
			vertices[index] = vertex_value;
			index++;
			

			// Second triangle in quad.
			vertex_value.position = D3DXVECTOR3( drawX, drawY, 0.0f );  // Top left.
			vertex_value.texture = D3DXVECTOR2( m_Font[letter].left, 0.0f );
			vertices[index] = vertex_value;
			index++;
			

			vertex_value.position = D3DXVECTOR3( drawX + m_Font[letter].size, drawY, 0.0f );  // Top right.
			vertex_value.texture = D3DXVECTOR2( m_Font[letter].right, 0.0f );
			vertices[index] = vertex_value;
			index++;
			

			vertex_value.position = D3DXVECTOR3( ( drawX + m_Font[letter].size ), ( drawY - 16 ), 0.0f );  // Bottom right.
			vertex_value.texture = D3DXVECTOR2( m_Font[letter].right, 1.0f );
			vertices[index] = vertex_value;
			index++;
			

			// Update the x location for drawing by the size of the letter and one pixel.
			drawX = drawX + m_Font[letter].size + 1.0f;
		}
	}

	return;
}