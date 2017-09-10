#pragma once

#include <d3dcommon.h>
#include <d3d11.h>
#include <d3dx10math.h>
#include "DMObject.h"


class DMClipMap :
	public DMObject
{
public:
	DMClipMap( DMObject* );
	virtual ~DMClipMap();

	bool Initialize( ID3D11Device* _device );

	void Render( ID3D11DeviceContext* _deviceContext, D3DXMATRIX* matrixProjection );

	void Shutdown();

private:
	bool InitializeBuffers( ID3D11Device* );
	void ShutdownBuffers( );
	bool UpdateBuffers( ID3D11DeviceContext* );
	void RenderBuffers( ID3D11DeviceContext*, D3DXMATRIX* matrixProjection );

private:
	ID3D11Buffer* m_vertexBuffer;
	ID3D11Buffer* m_indexBuffer;
	int m_vertexCount;
	int m_indexCount;
};

