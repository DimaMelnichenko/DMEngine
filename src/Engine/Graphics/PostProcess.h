#pragma once

#include <string>
#include <vector>
#include "FullscreenShader.h"
#include "ComputeShader.h"
#include "D3D\RenderTarget.h"
#include "D3D\ReadbackRing.h"
#include "D3D\GpuProfiler.h"
#include "Properties\PropertyContainer.h"
#include "Scene\Level\LevelSettings.h"
#include "Shaders\tonemap_constants.h"
#include "Shaders\bloom_constants.h"
#include "Shaders\exposure_histogram_constants.h"
#include "Shaders\exposure_adapt_constants.h"
#include "RenderFeature.h"

namespace GS
{

// Постобработка кадра — цепочка проходов с промежуточными целями (RenderTarget):
// - автоэкспозиция (Eye Adaptation в UE): гистограмма log₂ яркости кадра (Shaders/exposure_histogram.cs) и новая
//   экспозиция (Shaders/exposure_adapt.cs) в буфере на GPU. Буфер сцены хранит яркость × экспозицию прошлого кадра
//   (pre-exposure, Shaders/exposure.sh), иначе физические яркости переполнили бы R16F;
// - bloom (J. Jimenez, SIGGRAPH 2014): цвет сцены уменьшается по уровням 1/2 … 1/64 (Shaders/bloom_downsample.ps —
//   с порогом и защитой от «светлячков» на первом), затем уровни увеличиваются и складываются от мелкого к крупному
//   (Shaders/bloom_upsample.ps, BlendState::additive);
// - тонмаппинг (Shaders/tonemap.ps): цвет сцены + bloom, приведение к новой экспозиции, ночное зрение (сдвиг Пуркинье:
//   в темноте цвет уходит в синеватый монохромный), AgX / ACES — в задний буфер.
// Ночь темнее дня — кривая компенсации экспозиции от EV100 сцены (Exposure Compensation Curve в UE).
// Настройки — как Exposure, Tonemapper, Bloom в Post Process Volume UE: строка таблицы PostProcessSettings, на которую
// ссылается уровень (Levels.post_process), меняются в GUI («Post process»). Новый проход (LUT, сглаживание) — ещё одна
// цель и шаг в render()
class PostProcess : public RenderFeature
{
public:
	// Тонмаппинг и замер экспозиции, настройки — Level/LevelSettings.h
	using Tonemapper = GS::Tonemapper;
	using MeteringMode = GS::MeteringMode;

	static constexpr uint32_t bloomLevelCount = 6;
	// Вес каждого следующего, вдвое более широкого уровня относительно предыдущего: вклад уровней убывает от узкого
	// к широкому примерно вшестеро, как веса Bloom1…Bloom6 Tint в UE
	static constexpr float bloomLevelFalloff = 0.6f;
	static constexpr uint32_t histogramBinCount = 64;	// HISTOGRAM_BINS в Shaders/exposure_*.cs
	static constexpr uint32_t maxCurveKeys = maxExposureCurveKeys;
	static_assert( maxCurveKeys == MAX_CURVE_KEYS, "MAX_CURVE_KEYS in Shaders/exposure_adapt_constants.h" );

	// Строка PostProcessSettings (Level/LevelSettings.h)
	using Settings = PostProcessSettings;

	bool initialize( const Settings& settings );
	// Новый размер кадра: уровни bloom заново по размеру заднего буфера
	bool resize() override;
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Начало кадра: экспозиция прошлого кадра — шейдерам сцены (t105, pre-exposure), до проходов сцены
	void beginFrame( const RenderFrame& frame, FrameParameters& constants ) override { bindExposure(); }
	// Экспозиция кадра — пиксельным шейдерам сцены (t105)
	void bindExposure();
	// Экспозиция кадра — для объявлений проходов, которые её читают
	const ShaderView& exposureView() const { return m_exposureSRV; }
	// Рисует цепочку в задний буфер и оставляет его привязанным для GUI; время проходов — области profiler.
	// sceneColor — HDR-цвет сцены (SceneTargets::colorView), deltaTime — длительность кадра, с (скорость адаптации)
	void render( const ShaderView& sceneColor, GpuProfiler& profiler, float deltaTime );
	// Смена плана (camera cut, как у вида в UE): несколько кадров экспозиция адаптируется сразу, а не за секунды
	void cameraCut() override { m_cutFrames = cutFrameCount; }
	// EV100 кадра с отставанием на несколько кадров (копия с GPU без ожидания) — для статистики
	float ev100() const { return m_ev100; }
	PropertyContainer* properties() override;
	bool environment() const override { return true; }

private:
	// Раскладка — ExposureState в Shaders/exposure.sh
	struct ExposureState
	{
		float exposure;
		float sceneExposure;
		float ev100;
		float padding;
	};





	bool createExposureResources( float initialEV100, float exposureCompensation );
	bool createBloomTargets();
	void renderExposure( const ShaderView& sceneColor, float deltaTime );
	void readBackExposure();
	void renderBloom( const ShaderView& sceneColor, float threshold );
	void drawPass( const char* name, FullscreenShader& shader, const RenderTarget& target, const ShaderView& source,
				   BloomBuffer params, BlendState blend );

	FullscreenShader m_shader;
	FullscreenShader m_bloomDownsample;
	FullscreenShader m_bloomUpsample;
	ComputeShader m_histogramShader;
	ComputeShader m_adaptShader;
	RenderTarget m_bloom[bloomLevelCount];
	Buffer m_constantBuffer;
	Buffer m_bloomConstants;
	Buffer m_histogramConstants;
	Buffer m_adaptConstants;

	Buffer m_histogram;
	StorageView m_histogramUAV;
	ShaderView m_histogramSRV;
	Buffer m_exposureState;
	StorageView m_exposureUAV;
	ShaderView m_exposureSRV;
	// Копии состояния для чтения на CPU по кругу: читается самая свежая из тех, до которых GPU уже дошёл
	static constexpr uint32_t readbackCount = 6;
	static constexpr DXGI_FORMAT bloomFormat = DXGI_FORMAT_R11G11B10_FLOAT;
	ReadbackRing<ExposureState> m_exposureReadback;
	float m_ev100 = 0.0f;
	// Кадры до конца смены плана: за один кадр гистограмма с чужой экспозицией может упереться в край диапазона
	static constexpr uint32_t cutFrameCount = 4;
	uint32_t m_cutFrames = 0;

	PropertyContainer m_properties;
	std::vector<std::string> m_curveKeyNames;	// свойства ключей кривой компенсации — по одному на ключ
};

}
