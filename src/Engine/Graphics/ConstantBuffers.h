#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "D3D\GpuResources.h"
#include "Scene\RenderView.h"

namespace GS
{

// Матрица нормалей к мировой — обратная транспонированная (без сдвига): нормали остаются перпендикулярными
// поверхности и при неравномерном масштабе. У вырожденной матрицы (нулевой масштаб) обратной нет — берётся сама матрица
DirectX::XMMATRIX normalMatrix( const DirectX::XMMATRIX& world );

// Ветер кадра (Wind::parameters) — cb_wind* в Shaders/common.vs, изгиб — Shaders/wind.sh
struct WindParameters
{
	DirectX::XMFLOAT2 direction = DirectX::XMFLOAT2( 0.0f, 1.0f );	// куда дует, нормированное (x, z)
	float strength = 0.0f;
	float speed = 0.0f;			// м/с
	float gustMin = 0.0f;
	float gustMax = 0.0f;
	float gustSize = 1.0f;		// м
};

// Туман уровня кадра (VolumetricFog::frameParameters) — cb_fog* в Shaders/common.vs, Shaders/height_fog.sh
struct FogParameters
{
	// Слои: плотность, 1/м; высота, ниже которой она ровная, м; спад выше неё, 1/м; w — у первого расстояние до фона
	// неба, м, у второго — анизотропия рассеяния g
	DirectX::XMFLOAT4 layer0 = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	DirectX::XMFLOAT4 layer1 = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	DirectX::XMFLOAT3 albedo = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );
	float scale = 0.0f;				// нормировка света в объёме; 0 — тумана нет
	DirectX::XMFLOAT3 gridZ = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );	// слои объёма по глубине: (B, O, S)
	float volumeDistance = 0.0f;	// дальность объёма, м; 0 — только туман по формуле
};

// Что одно на весь кадр (все виды): время, свет, небо, ветер, туман
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
	FogParameters fog;
	// Тень облаков (VolumetricCloud::shadowParameters): начало карты X, Z; 1 / размер (0 — облаков нет); высота плоскости
	DirectX::XMFLOAT4 cloudShadow = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
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
	void setPerObjectBuffer( const DirectX::XMMATRIX& world, float lodDither = 0.0f );

private:
	struct alignas( 16 ) ShaderFrameConstant
	{
		DirectX::XMMATRIX view;
		DirectX::XMMATRIX viewInverse;
		DirectX::XMMATRIX projection;
		DirectX::XMMATRIX viewProjection;
		DirectX::XMFLOAT3 cameraPosition;
		float gameTime;
		DirectX::XMFLOAT3 viewDirection;
		float deltaTime;
		float lightsCount;
		DirectX::XMFLOAT3 lodOrigin;
		float skyLightScale;
		float aerialPerspectiveDistance;
		float aerialPerspectiveScale;
		float skyScale;
		DirectX::XMFLOAT2 windDirection;
		float windStrength;
		float windSpeed;
		float windGustMin;
		float windGustMax;
		float windGustSize;
		float framePadding;
		DirectX::XMFLOAT4 fogLayer0;
		DirectX::XMFLOAT4 fogLayer1;
		DirectX::XMFLOAT3 fogAlbedo;
		float fogScale;
		DirectX::XMFLOAT3 fogGridZ;
		float fogVolumeDistance;
		DirectX::XMFLOAT4 cloudShadow;
	};
	// Раскладка — cbuffer FrameConstantBuffer в Shaders/common.vs
	static_assert( sizeof( ShaderFrameConstant ) == 432, "FrameConstantBuffer layout" );

	// Раскладка — cbuffer WorldBuffer в Shaders/common.vs
	struct alignas( 16 ) ShaderModelConstant
	{
		DirectX::XMMATRIX world;
		DirectX::XMMATRIX worldInverseTranspose;
		float lodDither;
		DirectX::XMFLOAT3 padding;
	};
	static_assert( sizeof( ShaderModelConstant ) == 144, "WorldBuffer layout" );

	FrameParameters m_frame;
	Buffer m_frameConstant;
	Buffer m_modelConstant;
};

}