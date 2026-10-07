#pragma once

#include "SceneObject.h"
#include "DMComputeShader.h"
#include "FullscreenShader.h"
#include "Properties\PropertyContainer.h"
#include "Level\LevelSettings.h"

namespace GS
{

class SkyAtmosphere;
class Wind;

// Облака — слой над долиной, как Volumetric Cloud в UE5 (A. Schneider, SIGGRAPH 2015; S. Hillaire, SIGGRAPH 2016).
// Плотность — шумы Перлина — Уорли и Уорли и карта погоды (Shaders/cloud_noise.cs — один раз при загрузке), профиль по
// высоте и общее покрытие (Shaders/volumetric_cloud.sh). compute() каждый кадр:
// - карта тени облаков вокруг камеры (Shaders/volumetric_cloud.cs, mainShadow; слот сцены t111) — её читают все
//   приёмники тени солнца (Shaders/cloud_shadow.sh);
// - луч главного вида через слой в половине кадра (mainTrace): свет светила через облако, неба и земли, воздух до облака
//   по модели неба (таблицы SkyAtmosphere), смешение с прошлым кадром.
// Свой вызов в проходе sky кладёт облака поверх фона неба (Shaders/cloud_composite.ps). Облака плывут по ветру уровня
// (Wind: направление; скорость — своя), без ветра (-nowind) стоят. Только с атмосферой; строка VolumetricCloud уровня
// (Levels.volumetric_cloud), окно GUI «Volumetric cloud», подробно — docs/clouds.md
class VolumetricCloud : public SceneObject
{
public:
	using Settings = VolumetricCloudSettings;

	VolumetricCloud();

	bool initialize( const Settings& settings, SkyAtmosphere& atmosphere, Wind& wind );
	bool initialized() const { return m_initialized; }
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Тень облаков для констант кадра (cb_cloudShadow, Shaders/cloud_shadow.sh): начало карты X, Z; 1 / размер;
	// высота плоскости. Облака выключены — нули
	DirectX::XMFLOAT4 shadowParameters( const RenderView& view );

	void update( const FrameContext& frame ) override;
	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	PropertyContainer* properties() override { return &m_properties; }

private:
	// Константный буфер CS b4, раскладка как у VolumetricCloudBuffer в Shaders/volumetric_cloud.sh
	struct alignas( 16 ) Parameters
	{
		DirectX::XMMATRIX previousViewProjection;
		float layerBottom;
		float layerTop;
		float coverage;
		float density;
		DirectX::XMFLOAT3 albedo;
		float shapeScale;
		DirectX::XMFLOAT2 windOffset;
		float detailScale;
		float weatherScale;
		DirectX::XMFLOAT3 lightDirection;
		float historyWeight;
		DirectX::XMFLOAT3 lightColor;
		float maxDistance;
		DirectX::XMFLOAT2 shadowOrigin;
		float shadowSize;
		float shadowStrength;
		uint32_t traceSize[2];
		uint32_t frameIndex;
		float outputScale;
	};
	static_assert( sizeof( Parameters ) == 176, "VolumetricCloudBuffer layout" );

	// Константный буфер PS b4, раскладка как у CloudCompositeBuffer в Shaders/cloud_composite.ps
	struct alignas( 16 ) CompositeParameters
	{
		float cloudScale;
		float padding[3];
	};

	bool createNoise();
	bool createTraceTargets( uint32_t width, uint32_t height );
	void generateNoise();
	bool enabled();
	Parameters currentParameters( const FrameContext& frame );

	SkyAtmosphere* m_atmosphere = nullptr;
	Wind* m_wind = nullptr;
	bool m_initialized = false;
	PropertyContainer m_properties;

	DMComputeShader m_shapeShader;
	DMComputeShader m_detailShader;
	DMComputeShader m_weatherShader;
	DMComputeShader m_traceShader;
	DMComputeShader m_shadowShader;
	FullscreenShader m_compositeShader;
	Buffer m_constants;
	Buffer m_compositeConstants;

	// Шумы и карта погоды — считаются в первом кадре
	Texture m_shape;
	StorageView m_shapeUAV;
	ShaderView m_shapeSRV;
	Texture m_detail;
	StorageView m_detailUAV;
	ShaderView m_detailSRV;
	Texture m_weather;
	StorageView m_weatherUAV;
	ShaderView m_weatherSRV;
	bool m_noiseReady = false;

	// Облака половины кадра: этот кадр и прошлый по очереди
	Texture m_clouds[2];
	StorageView m_cloudsUAV[2];
	ShaderView m_cloudsSRV[2];
	uint32_t m_traceSize[2] = {};
	uint32_t m_current = 0;

	Texture m_shadow;
	StorageView m_shadowUAV;
	ShaderView m_shadowSRV;

	DirectX::XMFLOAT2 m_windOffset = DirectX::XMFLOAT2( 0.0f, 0.0f );
	DirectX::XMMATRIX m_previousViewProjection = DirectX::XMMatrixIdentity();
	DirectX::XMFLOAT3 m_previousCamera = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
	bool m_historyValid = false;
	uint32_t m_frameIndex = 0;		// с последней смены плана: снимки с одной точки совпадают между запусками
};

}
