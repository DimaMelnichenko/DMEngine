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
	// Мировая матрица объекта (b1) и матрица нормалей к ней — обратная транспонированная: нормали остаются
	// перпендикулярными поверхности и при неравномерном масштабе
	void setPerObjectBuffer( const XMMATRIX& world );

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

	// Раскладка — cbuffer WorldBuffer в Shaders/common.vs
	struct alignas( 16 ) ShaderModelConstant
	{
		XMMATRIX world;
		XMMATRIX worldInverseTranspose;
	};

	com_unique_ptr<ID3D11Buffer> m_frameConstant;
	com_unique_ptr<ID3D11Buffer> m_modelConstant;
	DMTimer m_timer;
};

}