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
	void setPerObjectBuffer( const XMMATRIX* );
	void setPerObjectBuffer( const XMMATRIX& );

private:
	struct alignas( 16 ) ShaderFrameConstant
	{
		XMMATRIX view;
		XMMATRIX viewInverse;
		XMMATRIX projection;
		XMMATRIX viewProjection;
		XMFLOAT3 cameraPosition;
		float appTime;
		XMFLOAT3 viewDirection;
		float elapsedTime;
		float lightsCount;
		XMFLOAT3 dump;
	};

	struct alignas( 16 ) ShaderModelConstant
	{
		XMMATRIX world;
	};

	com_unique_ptr<ID3D11Buffer> m_frameConstant;
	com_unique_ptr<ID3D11Buffer> m_modelConstant;
	DMTimer m_timer;
};

}