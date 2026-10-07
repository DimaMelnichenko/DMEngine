#pragma once

#include <optional>
#include "DMComputeShader.h"
#include "ConstantBuffers.h"
#include "Properties\PropertyContainer.h"
#include "Scene\Level\LevelSettings.h"
#include "Scene\RenderView.h"

namespace GS
{

// Туман уровня — как Exponential Height Fog с Volumetric Fog в UE (B. Wronski, SIGGRAPH 2014; S. Hillaire, SIGGRAPH 2015).
// Плотность — два слоя по высоте (Shaders/height_fog.sh). Ближе дальности объёма туман считается в сетке над экраном
// главного вида (Shaders/volumetric_fog.cs: ячейки VOLUMETRIC_FOG_TILE² пикселей × VOLUMETRIC_FOG_DEPTH слоёв, свет
// солнца с каскадной тенью — лучи в дымке, неба и ламп, сдвиг точки в ячейке по кадрам и смешение с прошлым кадром),
// дальше — по формуле вдоль луча без теней. Его получают все материалы с освещением (evaluateLighting), фон неба, вода
// и частицы — applyFogging. Настройки — строка ExponentialHeightFog уровня (Levels.height_fog), окно GUI «Height fog»;
// проходы — после карты теней, до проходов сцены (Renderer::render), подробно — docs/fog.md
class VolumetricFog
{
public:
	using Settings = HeightFogSettings;

	// settings — строка уровня; нет — тумана нет, пока его не включат в GUI («Enabled»)
	bool initialize( const std::optional<Settings>& settings );
	// Новый размер кадра: сетка — по размеру заднего буфера
	bool resize();
	// Константы кадра cb_fog* для главного вида. normalization — яркость, кд/м², которую экспозиция кадра делает белой
	// (по EV100): свет в объёме делится на неё, чтобы половинной точности хватало и днём, и ночью
	FogParameters frameParameters( const RenderView& view, float normalization );
	// Объём главного вида: свет в ячейках с прошлым кадром, накопление вдоль лучей и привязка (t110) проходам сцены.
	// После карты теней: каскады и их константы уже привязаны (ShadowCascades::bindForReceivers)
	void render( const RenderView& view, const ShaderView& shadowMap );
	// Объём считается в этом кадре (туман включён и объёмный)
	bool active();
	// Смена плана: прошлый кадр объёма не годится
	void cameraCut();
	// Текущие настройки из GUI — для сохранения уровня; нет строки и туман не включали — пусто
	std::optional<Settings> settings();
	PropertyContainer* properties() { return &m_properties; }

private:
	// Константный буфер CS b4, раскладка как у VolumetricFogBuffer в Shaders/volumetric_fog.cs
	struct alignas( 16 ) Parameters
	{
		DirectX::XMMATRIX previousViewProjection;
		DirectX::XMFLOAT3 jitter;
		float historyWeight;
		uint32_t gridSize[3];
		float historyScale;
		float inverseScale;
		float padding[3];
	};
	static_assert( sizeof( Parameters ) == 112, "VolumetricFogBuffer layout" );

	bool createVolumes();
	bool enabled();

	DMComputeShader m_lightShader;
	DMComputeShader m_integrateShader;
	Buffer m_constants;
	// Свет и плотность ячеек: этот кадр и прошлый по очереди
	Texture m_lighting[2];
	StorageView m_lightingUAV[2];
	ShaderView m_lightingSRV[2];
	// Накопленные от камеры свет и пропускание — его читают проходы сцены
	Texture m_integrated;
	StorageView m_integratedUAV;
	ShaderView m_integratedSRV;
	uint32_t m_gridSize[3] = {};
	uint32_t m_current = 0;

	bool m_historyValid = false;
	uint32_t m_jitterIndex = 0;		// с последней смены плана: снимки с одной точки совпадают между запусками
	DirectX::XMMATRIX m_previousViewProjection = DirectX::XMMatrixIdentity();
	float m_scale = 1.0f;			// нормировка этого кадра (frameParameters)
	float m_previousScale = 1.0f;
	bool m_hasRow = false;			// у уровня есть строка ExponentialHeightFog

	PropertyContainer m_properties;
};

}
