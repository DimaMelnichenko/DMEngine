#include "DMTexture.h"
#include <D3DX11tex.h>
#include "utilites.h"


DMTexture::DMTexture( DMD3D* parent ) : DM3DObject( parent )
{
	m_texture = nullptr;

}


DMTexture::~DMTexture()
{

}

bool DMTexture::Initialize( const WCHAR* filename )
{
	HRESULT result;

	// Load the texture in.
	ID3D11ShaderResourceView* texture;
	result = D3DX11CreateShaderResourceViewFromFile( m_dmd3d->GetDevice(), filename, NULL, NULL, &texture, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	m_texture = make_com_ptr<ID3D11ShaderResourceView>( texture );

	return true;
}

ID3D11ShaderResourceView* DMTexture::GetTexture()
{
	return m_texture.get();
}

