#pragma once

//////////////
// INCLUDES //
//////////////
#include "D3D/DMD3D.h"

class DMTexture
{		
public:
	DMTexture();
	DMTexture( DMTexture&& other );
	~DMTexture();

	bool Initialize( const WCHAR* );
	ID3D11ShaderResourceView* GetTexture( );

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_texture;
	
};

