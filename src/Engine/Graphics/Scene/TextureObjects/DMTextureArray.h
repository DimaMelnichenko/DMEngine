#pragma once

//////////////
// INCLUDES //
//////////////

#include "..\DMSceneObject.h"

class DMTextureArray : public DMSceneObject
{
public:
	DMTextureArray( DMD3D* );
	~DMTextureArray();

	bool Initialize( const WCHAR* );
	bool AddTexture( const WCHAR* );
	void Shutdown( );	

	ID3D11ShaderResourceView** GetTextureArray( );
	int count( );

private:
	std::vector<ID3D11ShaderResourceView*> m_textures;	
};

