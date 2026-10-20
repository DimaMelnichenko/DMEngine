#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "D3D\GpuResources.h"
#include "Scene\RenderView.h"
#include "Shaders\frame_constants.h"
#include "Scene\Wind\WindParameters.h"

namespace GS
{

// Матрица нормалей к мировой — обратная транспонированная (без сдвига): нормали остаются перпендикулярными
// поверхности и при неравномерном масштабе. У вырожденной матрицы (нулевой масштаб) обратной нет — берётся сама матрица
DirectX::XMMATRIX normalMatrix( const DirectX::XMMATRIX& world );

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
	// Детальная вода у русел (WaterSimulation::detailParameters, water.sh): размер мира, м; сторона плитки, м (0 — её нет)
	DirectX::XMFLOAT4 waterDetail = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	// Следы в траве (FoliageInteraction::frameParameters, foliage_interaction.sh): начало окна X, Z; 1 / сторона (0 — поля
	// нет); наибольший наклон, рад
	DirectX::XMFLOAT4 foliageInteraction = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	// Экранное затенение окружением (AmbientOcclusion::frameParameters): сила (0 — нет), размер текстуры
	DirectX::XMFLOAT4 ambientOcclusion = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	// Карты отражений места (ReflectionCaptures::frameConstants) — две ближайшие к камере: точка съёмки и радиус влияния
	// (0 — карты нет); множитель выборки, 1 / полоса перехода в небо
	DirectX::XMFLOAT4 reflectionCaptureSphere[2] = {};
	DirectX::XMFLOAT4 reflectionCapture[2] = {};
	// Террейн уровня (TerrainPipeline::frameConstants): сторона карты, м (0 — нет)
	DirectX::XMFLOAT4 terrain = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
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
	void setViewBuffer( const RenderView& view ) { setViewBuffer( view, m_frame ); }
	// То же со своими данными кадра — у вида съёмки сцены (Renderer::renderCapture) без ресурсов главного вида: затенения
	// окружением по экрану, объёма тумана
	void setViewBuffer( const RenderView& view, const FrameParameters& frame );
	// Данные кадра (beginFrame) — основа своих у вида съёмки
	const FrameParameters& frameParameters() const { return m_frame; }
	// Мировая матрица объекта (b1) и матрица нормалей к ней — обратная транспонированная: нормали остаются
	// перпендикулярными поверхности и при неравномерном масштабе
	// lodDither — доля смены LOD дизерингом у экземпляра в полосе перехода (Shaders/lod_dither.sh), 0 — вне её; boneOffset —
	// первая матрица скиннинга объекта в буфере вызова (Shaders/skinning.sh, SkinningPalette)
	void setPerObjectBuffer( const DirectX::XMMATRIX& world, float lodDither = 0.0f, uint32_t boneOffset = 0 );

private:
	FrameParameters m_frame;
	Buffer m_frameConstant;
	Buffer m_modelConstant;
};

}