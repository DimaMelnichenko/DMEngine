#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Utils\DMTimer.h"
#include "Scene\Camera\DMCamera.h"

namespace GS
{

class ConstantBuffers
{

public:
	ConstantBuffers();
	~ConstantBuffers();

	void initBuffers();
	void setPerFrameBuffer( const DMCamera&, int lightsCount );
	void setPerObjectBuffer( const D3DXMATRIX* );

private:
	struct alignas( 16 ) ShaderFrameConstant
	{
		D3DXMATRIX view;
		D3DXMATRIX viewInverse;
		D3DXMATRIX projection;
		D3DXMATRIX viewProjection;
		D3DXVECTOR3 cameraPosition;
		float appTime;
		D3DXVECTOR3 viewDirection;
		float elapsedTime;
		float lightsCount;
	};

	struct alignas( 16 ) ShaderModelConstant
	{
		D3DXMATRIX world;
	};

	com_unique_ptr<ID3D11Buffer> m_frameConstant;
	com_unique_ptr<ID3D11Buffer> m_modelConstant;
	DMTimer m_timer;
};

}