#pragma once

//////////////
// INCLUDES //
//////////////

#include "..\DMSceneObject.h"
#include "Utils\utilites.h"
#include "D3D\DMD3D.h"

class DMTextureArray : public DMSceneObject
{
public:
	DMTextureArray();
	~DMTextureArray();

	bool Initialize( const WCHAR* );
	bool AddTexture( const WCHAR* );

	ID3D11ShaderResourceView** GetTextureArray();
	int count( ) const;

private:
	std::vector<ID3D11ShaderResourceView*> m_textures;
};

