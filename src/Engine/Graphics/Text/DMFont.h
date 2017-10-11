#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>

///////////////////////
// MY CLASS INCLUDES //
///////////////////////

#include "..\Scene\TextureObjects\DMTexture.h"

class DMFont
{
private:
	struct FontType
	{
		float left, right;
		int size;
	};

	
	

public:
	DMFont();
	~DMFont();

	bool Initialize( char*, WCHAR* );


	ID3D11ShaderResourceView* GetTexture( );

	struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
	};

	void BuildVertexArray( std::vector<VertexType>&, char*, float, float );

private:
	bool LoadFontData( char* );
	void ReleaseFontData( );
	bool LoadTexture( WCHAR* );

private:
	std::vector<FontType> m_Font;
	DMTexture m_Texture;

};

