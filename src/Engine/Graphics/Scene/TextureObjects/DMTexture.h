#pragma once

//////////////
// INCLUDES //
//////////////
#include "..\..\Common\DM3DObject.h"

class DMTexture : public DM3DObject
{
public:	
		
public:
	DMTexture( DMD3D* );
	~DMTexture();

	bool Initialize( const WCHAR* );
	ID3D11ShaderResourceView* GetTexture( );

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_texture;
	
};

