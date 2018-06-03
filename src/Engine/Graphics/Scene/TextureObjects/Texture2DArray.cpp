#include "Texture2DArray.h"
#include "System.h"


Texture2DArray::Texture2DArray( size_t count )
{

}


Texture2DArray::~Texture2DArray()
{
	
}

bool Texture2DArray::Initialize( const char* filename )
{
	return AddTexture( filename );
}

bool Texture2DArray::AddTexture( const char* filename )
{
	D3DX11CreateTextureFromFile(  );

	HRESULT result;
	ID3D11ShaderResourceView* texture;

	result = D3DX11CreateShaderResourceViewFromFile( DMD3D::instance().GetDevice(), filename, NULL, NULL, &texture, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	m_textures.push_back( texture );

	return true;
}

ID3D11ShaderResourceView** Texture2DArray::GetTextureArray()
{
	return &m_textures[0];
}

int Texture2DArray::count( ) const
{
	return m_textures.size();
}