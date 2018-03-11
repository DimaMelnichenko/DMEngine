#pragma once

//////////////
// INCLUDES //
//////////////
#include "D3D/DMD3D.h"
#include "DMResource.h"

class DMTexture : public DMResource
{		
public:
	DMTexture( uint32_t id );
	~DMTexture();

	bool Initialize( const WCHAR* );
	ID3D11ShaderResourceView* GetTexture( );

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_texture;
	
};

