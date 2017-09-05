#include "DMTextureArray.h"
#include "utilites.h"


DMTextureArray::DMTextureArray( DMD3D* parent ) : DMSceneObject( parent )
{

}


DMTextureArray::~DMTextureArray()
{
	Shutdown();
}

bool DMTextureArray::Initialize( const WCHAR* filename )
{
	return AddTexture( filename );
}

bool DMTextureArray::AddTexture( const WCHAR* filename )
{
	HRESULT result;
	ID3D11ShaderResourceView* texture;

	result = D3DX11CreateShaderResourceViewFromFile( m_dmd3d->GetDevice(), filename, NULL, NULL, &texture, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	m_textures.push_back( texture );

	return true;
}

void DMTextureArray::Shutdown( )
{
	for( ID3D11ShaderResourceView* texture : m_textures )
	{
		delete_com( texture );
	}

	return;
}

ID3D11ShaderResourceView** DMTextureArray::GetTextureArray( )
{
	return &m_textures[0];
}

int DMTextureArray::count( )
{
	return m_textures.size();
}