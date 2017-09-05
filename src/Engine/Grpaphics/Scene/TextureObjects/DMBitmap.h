#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>


///////////////////////
// MY CLASS INCLUDES //
///////////////////////
#include "DMTexture.h"
#include "DMSceneObject.h"

class DMBitmap : public DMSceneObject
{
private:
	struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
	};

public:
	DMBitmap( DMD3D* );
	~DMBitmap();

	bool Initialize( int, int, int, int );		
	bool Render( int, int );

	int GetIndexCount( );

private:
	bool InitializeBuffers();	
	bool UpdateBuffers( int, int );
	void RenderBuffers();

private:	
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_indexBuffer;
	int m_vertexCount, m_indexCount;

	int m_screenWidth, m_screenHeight;
	int m_bitmapWidth, m_bitmapHeight;
	int m_previousPosX, m_previousPosY;
};

