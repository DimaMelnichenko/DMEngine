#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"


///////////////////////
// MY CLASS INCLUDES //
///////////////////////

#include "..\DMSceneObject.h"
#include "..\TextureObjects\DMTexture.h"

class DMBitmap : public DMSceneObject
{
private:
	struct VertexType
	{
		XMFLOAT3 position;
		XMFLOAT2 texture;
	};

public:
	DMBitmap();
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

