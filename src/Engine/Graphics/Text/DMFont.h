#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include <fstream>

///////////////////////
// MY CLASS INCLUDES //
///////////////////////
#include "..\Common\DM3DObject.h"
#include "..\Scene\TextureObjects\DMTexture.h"

class DMFont : public DM3DObject
{
private:
	struct FontType
	{
		float left, right;
		int size;
	};

	
	

public:
	DMFont( DMD3D* );
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
	com_unique_ptr<DMTexture> m_Texture;

};

