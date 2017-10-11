#pragma once


//////////////
// INCLUDES //
//////////////
#include "DirectX.h"

class DMDebugWindow
{
private:
	struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
	};

public:
	DMDebugWindow();
	~DMDebugWindow();

	bool Initialize(ID3D11Device* device, int screenWidth, int screenHeight, int bitmapWidth, int bitmapHeight);
	void Shutdown( );
	bool Render( ID3D11DeviceContext* deviceContext, int positionX, int positionY );

	int GetIndexCount( );

private:
	bool InitializeBuffers( ID3D11Device* );
	void ShutdownBuffers( );
	bool UpdateBuffers( ID3D11DeviceContext*, int, int );
	void RenderBuffers( ID3D11DeviceContext* );

private:
	ID3D11Buffer *m_vertexBuffer, *m_indexBuffer;
	int m_vertexCount, m_indexCount;
	int m_screenWidth, m_screenHeight;
	int m_bitmapWidth, m_bitmapHeight;
	int m_previousPosX, m_previousPosY;
};

