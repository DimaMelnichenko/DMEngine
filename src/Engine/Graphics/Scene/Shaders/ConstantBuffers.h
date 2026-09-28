#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Scene\RenderView.h"

namespace GS
{

// Матрица нормалей к мировой — обратная транспонированная (без сдвига): нормали остаются перпендикулярными
// поверхности и при неравномерном масштабе. У вырожденной матрицы (нулевой масштаб) обратной нет — берётся сама матрица
XMMATRIX normalMatrix( const XMMATRIX& world );

// Ветер кадра (Wind::parameters) — cb_wind* в Shaders/common.vs, изгиб — Shaders/wind.sh
struct WindParameters
{
	XMFLOAT2 direction = XMFLOAT2( 0.0f, 1.0f );	// куда дует, нормированное (x, z)
	float strength = 0.0f;
	float speed = 0.0f;			// м/с
	float gustMin = 0.0f;
	float gustMax = 0.0f;
	float gustSize = 1.0f;		// м
};

// Что одно на весь кадр (все виды): время, свет, небо, ветер
struct FrameParameters
{
	int lightsCount = 0;
	// Масштаб освещения окружением и фона неба (освещённость от солнца у атмосферы, интенсивность у панорамы)
	float skyLightScale = 0.0f;
	float skyScale = 0.0f;
	// Дальность слоёв воздушной перспективы (дальняя плоскость главного вида, 0 — перспективы нет) и её множитель
	float aerialPerspectiveDistance = 0.0f;
	float aerialPerspectiveScale = 0.0f;
	// Время игры и длительность кадра, с — шагами кадра: с timestep идут фиксированными шагами (View.GameTime /
	// DeltaTime в UE)
	float gameTime = 0.0f;
	float deltaTime = 0.0f;
	WindParameters wind;
};

class ConstantBuffers
{

public:
	ConstantBuffers();
	~ConstantBuffers();

	void initBuffers();
	// Один раз за кадр: то, что одно на все виды кадра
	void beginFrame( const FrameParameters& frame );
	// После compute: пересчёт освещения окружением мог закончиться и сменить нормировку (действует со следующего setViewBuffer)
	void setSkyLightScale( float skyLightScale ) { m_frame.skyLightScale = skyLightScale; }
	// Константы кадра (b0) для вида: матрицы и положение камеры; время и свет — от beginFrame. Для каждого вида
	// кадра (главная камера, позже каскады теней) — свой вызов
	void setViewBuffer( const RenderView& view );
	// Мировая матрица объекта (b1) и матрица нормалей к ней — обратная транспонированная: нормали остаются
	// перпендикулярными поверхности и при неравномерном масштабе
	// lodDither — доля смены LOD дизерингом у экземпляра в полосе перехода (Shaders/lod_dither.sh), 0 — вне её
	void setPerObjectBuffer( const XMMATRIX& world, float lodDither = 0.0f );

private:
	struct alignas( 16 ) ShaderFrameConstant
	{
		XMMATRIX view;
		XMMATRIX viewInverse;
		XMMATRIX projection;
		XMMATRIX viewProjection;
		XMFLOAT3 cameraPosition;
		float gameTime;
		XMFLOAT3 viewDirection;
		float deltaTime;
		float lightsCount;
		XMFLOAT3 lodOrigin;
		float skyLightScale;
		float aerialPerspectiveDistance;
		float aerialPerspectiveScale;
		float skyScale;
		XMFLOAT2 windDirection;
		float windStrength;
		float windSpeed;
		float windGustMin;
		float windGustMax;
		float windGustSize;
		float framePadding;
	};
	// Раскладка — cbuffer FrameConstantBuffer в Shaders/common.vs
	static_assert( sizeof( ShaderFrameConstant ) == 352, "FrameConstantBuffer layout" );

	// Раскладка — cbuffer WorldBuffer в Shaders/common.vs
	struct alignas( 16 ) ShaderModelConstant
	{
		XMMATRIX world;
		XMMATRIX worldInverseTranspose;
		float lodDither;
		XMFLOAT3 padding;
	};
	static_assert( sizeof( ShaderModelConstant ) == 144, "WorldBuffer layout" );

	FrameParameters m_frame;
	com_unique_ptr<ID3D11Buffer> m_frameConstant;
	com_unique_ptr<ID3D11Buffer> m_modelConstant;
};

}