#include "DMTextureArray.h"


DMTextureArray::DMTextureArray() : DMSceneObject()
{

}


DMTextureArray::~DMTextureArray()
{
	for( auto p : m_textures )
	{
		p->Release();
		p = nullptr;
	}
}

bool DMTextureArray::Initialize( const WCHAR* filename )
{
	return AddTexture( filename );
}

bool DMTextureArray::AddTexture( const WCHAR* filename )
{
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

ID3D11ShaderResourceView** DMTextureArray::GetTextureArray()
{
	return &m_textures[0];
}

int DMTextureArray::count( ) const
{
	return m_textures.size();
}