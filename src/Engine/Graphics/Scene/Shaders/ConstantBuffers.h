#pragma once
#include "DirectX.h"
#include "Scene\Camera\DMCamera.h"

namespace GS
{

class CommonConstant
{

public:
	CommonConstant();
	~CommonConstant();

	void updateCamera( const DMCamera& );

private:
	struct alignas( 16 ) ShaderFrameConstant
	{
		D3DXMATRIX view;
		D3DXMATRIX viewInverse;
		D3DXMATRIX projection;
		D3DXMATRIX viewProjection;
		D3DXVECTOR3 cameraPosition;
		D3DXVECTOR3 viewDirection;
		float appTime;
		float elapsedTime;
	};

	struct alignas( 16 ) ShaderModelConstant
	{
		D3DXMATRIX world;
	};
};

}