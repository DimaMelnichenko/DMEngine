#pragma once

#include "SceneObject.h"
#include "ComputeShader.h"
#include "FullscreenShader.h"
#include "Properties\PropertyContainer.h"
#include "Level\LevelSettings.h"
#include "Shaders\volumetric_cloud_constants.h"
#include "Shaders\cloud_composite_constants.h"

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
	void update( const FrameContext& frame ) override;
	// Тень облаков в константах кадра (cb_cloudShadow, Shaders/cloud_shadow.sh): её читают все приёмники тени солнца
	void frameConstants( const RenderView& view, FrameParameters& constants ) override;
	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	PropertyContainer* properties() override { return &m_properties; }
	bool environment() const override { return true; }

private:


	// Тень облаков (cb_cloudShadow): начало карты X, Z; 1 / размер; высота плоскости. Облака выключены — нули
	DirectX::XMFLOAT4 shadowParameters( const RenderView& view );
	bool createNoise();
	bool createTraceTargets( uint32_t width, uint32_t height );
	void generateNoise();
	bool enabled();
	VolumetricCloudBuffer currentParameters( const FrameContext& frame );

	SkyAtmosphere* m_atmosphere = nullptr;
	Wind* m_wind = nullptr;
	bool m_initialized = false;
	PropertyContainer m_properties;

	ComputeShader m_shapeShader;
	ComputeShader m_detailShader;
	ComputeShader m_weatherShader;
	ComputeShader m_traceShader;
	ComputeShader m_shadowShader;
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
