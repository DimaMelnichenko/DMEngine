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
#include "Shaders\star_constants.h"
#include "Shaders\light_shaft_constants.h"
#include "Shaders\exposure_histogram_constants.h"
#include "Shaders\exposure_adapt_constants.h"
#include "RenderFeature.h"
#include "ColorGradingLut.h"

namespace GS
{

// Постобработка кадра — цепочка проходов с промежуточными целями (RenderTarget):
// - автоэкспозиция (Eye Adaptation в UE): гистограмма log₂ яркости кадра (Shaders/exposure_histogram.cs) и новая
//   экспозиция (Shaders/exposure_adapt.cs) в буфере на GPU. Буфер сцены хранит яркость × экспозицию прошлого кадра
//   (pre-exposure, Shaders/exposure.sh), иначе физические яркости переполнили бы R16F;
// - bloom (J. Jimenez, SIGGRAPH 2014): цвет сцены уменьшается по уровням 1/2 … 1/64 (Shaders/bloom_downsample.ps —
//   с порогом и защитой от «светлячков» на первом), затем уровни увеличиваются и складываются от мелкого к крупному
//   (Shaders/bloom_upsample.ps, BlendState::additive);
// - лучи-звёзды вокруг ярких бликов (M. Kawase, GDC 2003): яркое над высоким порогом в четверть кадра (Shaders/star_bright.ps)
//   и по оси звезды три прохода вдоль неё с шагом 1, 4, 16 текселей (Shaders/star_streak.ps), оси — в сумму;
// - лучи солнца, god rays (K. Mitchell, GPU Gems 3, гл. 13; Light Shafts в UE): маска неба у солнца в половине кадра — до
//   полупрозрачных (renderLightShaftMask, Shaders/light_shaft_mask.ps), здесь — радиальное размытие к экранной точке солнца
//   одним маршем со сдвигом выборок шумом (Shaders/light_shaft_blur.ps);
// - тонмаппинг (Shaders/tonemap.ps): цвет сцены + bloom + лучи, приведение к новой экспозиции, ночное зрение (сдвиг Пуркинье:
//   в темноте цвет уходит в синеватый монохромный), затем цветокоррекция и кривая AgX / ACES — одной выборкой таблицы
//   (ColorGradingLut, как Combine LUTs в UE: строится в compute после правки настроек) — в задний буфер.
// Ночь темнее дня — кривая компенсации экспозиции от EV100 сцены (Exposure Compensation Curve в UE).
// Настройки — как Exposure, Tonemapper, Bloom, Color Grading в Post Process Volume UE: строка таблицы PostProcessSettings, на
// которую ссылается уровень (Levels.post_process), меняются в GUI («Post process», цветокоррекция — подокно «Color grading»).
// Новый проход (сглаживание) — ещё одна цель и шаг в render()
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
	// Настройки пресета окружения (на лету) — в окно и подокна; ключи кривой компенсации — сколько их в окне
	void apply( const Settings& settings );
	// Начало кадра: экспозиция прошлого кадра — шейдерам сцены (t105, pre-exposure), до проходов сцены
	void beginFrame( const RenderFrame& frame, FrameParameters& constants ) override { bindExposure(); }
	// Экспозиция кадра — пиксельным шейдерам сцены (t105)
	void bindExposure();
	// Экспозиция кадра — для объявлений проходов, которые её читают
	const ShaderView& exposureView() const { return m_exposureSRV; }
	// Маска лучей солнца — после неба, до полупрозрачных (частицы и вода глубину не пишут и на фоне неба дали бы свои лучи):
	// sceneColor, sceneDepth — буфер сцены, view — главный вид, toSun — направление на солнце (ночью — на луну; y ≤ 0 — его
	// нет). Солнца нет в кадре и рядом — маски нет, и лучей в этом кадре тоже
	void renderLightShaftMask( const ShaderView& sceneColor, const ShaderView& sceneDepth, const RenderView& view,
							   const DirectX::XMFLOAT3& toSun, GpuProfiler& profiler );
	// Рисует цепочку в задний буфер и оставляет его привязанным для GUI; время проходов — области profiler.
	// sceneColor — HDR-цвет сцены (SceneTargets::colorView), sceneDepth — её глубина (небо — 0), view — главный вид,
	// deltaTime — длительность кадра, с (скорость адаптации)
	void render( const ShaderView& sceneColor, const ShaderView& sceneDepth, const RenderView& view, GpuProfiler& profiler,
				 float deltaTime );
	// Смена плана (camera cut, как у вида в UE): несколько кадров экспозиция адаптируется сразу, а не за секунды
	void cameraCut() override { m_cutFrames = cutFrameCount; }
	// EV100 кадра с отставанием на несколько кадров (копия с GPU без ожидания) — для статистики
	float ev100() const { return m_ev100; }
	// Множитель яркости, с которым рисуется сцена (preExposure), — с той же задержкой чтения, что ev100
	float exposure() const { return m_exposure; }
	// -nocolorgrading: кадр без цветокоррекции (только кривая тонмаппинга), как ShowFlag.ColorGrading 0 в UE — снимки
	// контрольных камер сравнимы с эталоном, как бы её ни настроили на уровне. Флажок «Enabled» окна, не данные уровня
	void disableColorGrading();
	PropertyContainer* properties() override;
	// Подокно по имени («Ambient occlusion», «Light shafts» …): отладочные виды меню View; nullptr — нет такого
	PropertyContainer* part( const char* name );
	// Отладочный вид «Ambient occlusion»: кадр — само затенение серым
	bool showAmbientOcclusion() const;
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
	// Лучи-звёзды: яркое в четверть кадра, оси звезды — по три прохода, сумма осей — в m_star
	void renderStars( const ShaderView& sceneColor, const Settings& settings );
	void drawStarPass( const char* name, FullscreenShader& shader, const RenderTarget& target, const ShaderView& source,
					   const StarBuffer& params, BlendState blend );
	// Лучи солнца: марш по маске (renderLightShaftMask) к экранной точке солнца — в m_shafts
	void renderLightShafts( const Settings& settings );
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
	FullscreenShader m_starBright;
	FullscreenShader m_starStreak;
	// Четверть кадра: яркое над порогом, два промежуточных прохода оси и сумма осей звезды
	RenderTarget m_starBrightTarget;
	RenderTarget m_starPing;
	RenderTarget m_starPong;
	RenderTarget m_star;
	Buffer m_starConstants;
	// Лучи солнца в половине кадра (в четверти след размывался и поперёк лучей): маска и след; экранная точка солнца и
	// видимость эффекта — из renderLightShaftMask этого кадра
	FullscreenShader m_shaftMask;
	FullscreenShader m_shaftBlur;
	RenderTarget m_shaftMaskTarget;
	RenderTarget m_shafts;
	Buffer m_shaftConstants;
	DirectX::XMFLOAT2 m_shaftSunUV = DirectX::XMFLOAT2( 0.5f, 0.5f );
	float m_shaftVisibility = 0.0f;
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
	float m_exposure = 1.0f;
	// Кадры до конца смены плана: за один кадр гистограмма с чужой экспозицией может упереться в край диапазона
	static constexpr uint32_t cutFrameCount = 4;
	uint32_t m_cutFrames = 0;

	// Цветокоррекция и тонмаппинг — таблица, строится заново после правки подокна «Color grading» или смены кривой
	ColorGradingLut m_colorGrading;

	PropertyContainer m_properties;
	// Подокна по эффектам (как разделы Post Process Volume в UE): экспозиция, bloom, звёзды, лучи солнца, ночное зрение,
	// затенение окружением — порядок и группы описания полей — postProcessParts в PostProcess.cpp
	PropertyContainer m_parts[6];
	std::vector<std::string> m_curveKeyNames;	// свойства ключей кривой компенсации (подокно «Exposure») — по одному на ключ
	// Подокно «Color grading» (баланс белого) и в нём части кадра: весь кадр, тени, полутона, света
	PropertyContainer m_colorGradingProperties;
	PropertyContainer m_colorGradingParts[4];
};

}
