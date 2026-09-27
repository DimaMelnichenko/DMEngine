#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Utils\DMTimer.h"
#include "Scene\RenderView.h"

namespace GS
{

// Матрица нормалей к мировой — обратная транспонированная (без сдвига): нормали остаются перпендикулярными
// поверхности и при неравномерном масштабе. У вырожденной матрицы (нулевой масштаб) обратной нет — берётся сама матрица
XMMATRIX normalMatrix( const XMMATRIX& world );

class ConstantBuffers
{

public:
	ConstantBuffers();
	~ConstantBuffers();

	void initBuffers();
	// Один раз за кадр: время, число источников света, масштаб неба и освещения окружением (освещённость от солнца
	// у атмосферы, интенсивность у панорамы) и дальность слоёв воздушной перспективы (дальняя плоскость главного вида,
	// 0 — перспективы нет)
	void beginFrame( int lightsCount, float skyLightScale, float aerialPerspectiveDistance );
	// Константы кадра (b0) для вида: матрицы и положение камеры; время и свет — от beginFrame. Для каждого вида
	// кадра (главная камера, позже каскады теней) — свой вызов
	void setViewBuffer( const RenderView& view );
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
		XMFLOAT3 lodOrigin;
		float skyLightScale;
		float aerialPerspectiveDistance;
		XMFLOAT2 padding;
	};

	// Раскладка — cbuffer WorldBuffer в Shaders/common.vs
	struct alignas( 16 ) ShaderModelConstant
	{
		XMMATRIX world;
		XMMATRIX worldInverseTranspose;
	};

	float m_lightsCount = 0.0f;
	float m_skyLightScale = 0.0f;
	float m_aerialPerspectiveDistance = 0.0f;
	com_unique_ptr<ID3D11Buffer> m_frameConstant;
	com_unique_ptr<ID3D11Buffer> m_modelConstant;
	DMTimer m_timer;
};

}