#pragma once

#include <string>
#include <vector>
#include "Shaders\FullscreenShader.h"
#include "Shaders\DMComputeShader.h"
#include "D3D\RenderTarget.h"
#include "D3D\ReadbackRing.h"
#include "D3D\GpuProfiler.h"
#include "Properties\PropertyContainer.h"

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
class PostProcess
{
public:
	enum class Tonemapper : int32_t
	{
		none = 0,	// только ограничение 0…1 — для отладки
		aces = 1,	// ACES (подгонка RRT + ODT, S. Hill) — основа Filmic tonemapper UE
		agx = 2		// AgX — стандартное отображение Blender 4+, мягче уводит яркие цвета в белый
	};

	// Metering Mode в UE: экспозиция задана или замеряется по гистограмме кадра
	enum class MeteringMode : int32_t
	{
		manual = 0,
		autoHistogram = 1
	};

	static constexpr uint32_t bloomLevelCount = 6;
	// Вес каждого следующего, вдвое более широкого уровня относительно предыдущего: вклад уровней убывает от узкого
	// к широкому примерно вшестеро, как веса Bloom1…Bloom6 Tint в UE
	static constexpr float bloomLevelFalloff = 0.6f;
	static constexpr uint32_t histogramBinCount = 64;	// HISTOGRAM_BINS в Shaders/exposure_*.cs
	static constexpr uint32_t maxCurveKeys = 8;			// MAX_CURVE_KEYS в Shaders/exposure_adapt.cs

	// Строка PostProcessSettings; без неё — значения по умолчанию (как у Post Process Volume в UE)
	struct Settings
	{
		MeteringMode meteringMode = MeteringMode::autoHistogram;
		float manualEV100 = 15.0f;			// Manual; с него же начинается автоэкспозиция
		float exposureCompensation = 0.0f;	// EV: +1 — вдвое ярче
		// Exposure Compensation Curve: ключи (EV100 сцены, поправка EV) по возрастанию EV100, между ними — линейно,
		// за крайними — значение крайнего; пусто — без кривой. Только для автоэкспозиции
		std::vector<XMFLOAT2> exposureCompensationCurve;
		float minEV100 = -10.0f;			// пределы автоэкспозиции и диапазон гистограммы
		float maxEV100 = 20.0f;
		float histogramLowPercent = 10.0f;	// процентили гистограммы: темнее и ярче — не в среднем
		float histogramHighPercent = 90.0f;
		float speedUp = 3.0f;				// скорость адаптации к более яркой сцене, 1/с
		float speedDown = 1.0f;				// к более тёмной
		Tonemapper tonemapper = Tonemapper::agx;
		float bloomIntensity = 0.05f;		// Bloom Intensity — доля энергии над порогом в свечении; 0 — без bloom
		float bloomThreshold = 1.0f;		// Bloom Threshold, яркость после экспозиции; < 0 — без порога
		float purkinjeShift = 1.0f;			// сила ночного зрения 0…1; 0 — цвет как днём при любой яркости
	};
	// Имена в базе: тонмаппинг None, ACES, AgX (другое — AgX); замер Manual, AutoHistogram (другое — AutoHistogram)
	static Tonemapper tonemapperFromName( const std::string& name );
	static const char* tonemapperName( Tonemapper tonemapper );
	static MeteringMode meteringModeFromName( const std::string& name );
	static const char* meteringModeName( MeteringMode mode );
	// Кривая в базе — текст «EV100,EV; EV100,EV; …»: ключи сортируются, лишние сверх maxCurveKeys отбрасываются
	static std::vector<XMFLOAT2> curveFromText( const std::string& text );
	static std::string curveText( const std::vector<XMFLOAT2>& curve );

	bool initialize( const Settings& settings );
	// Новый размер кадра: уровни bloom заново по размеру заднего буфера
	bool resize();
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Экспозиция кадра — пиксельным шейдерам сцены (t105), до проходов сцены
	void bindExposure();
	// Экспозиция кадра — для объявлений проходов, которые её читают
	const ShaderView& exposureView() const { return m_exposureSRV; }
	// Рисует цепочку в задний буфер и оставляет его привязанным для GUI; время проходов — области profiler.
	// sceneColor — HDR-цвет сцены (SceneTargets::colorView), deltaTime — длительность кадра, с (скорость адаптации)
	void render( const ShaderView& sceneColor, GpuProfiler& profiler, float deltaTime );
	// Смена плана (camera cut, как у вида в UE): несколько кадров экспозиция адаптируется сразу, а не за секунды
	void cameraCut() { m_cutFrames = cutFrameCount; }
	// EV100 кадра с отставанием на несколько кадров (копия с GPU без ожидания) — для статистики
	float ev100() const { return m_ev100; }
	PropertyContainer* properties();

private:
	// Раскладка — ExposureState в Shaders/exposure.sh
	struct ExposureState
	{
		float exposure;
		float sceneExposure;
		float ev100;
		float padding;
	};

	// Константный буфер CS b4, раскладка как у HistogramBuffer в Shaders/exposure_histogram.cs
	struct alignas( 16 ) HistogramParameters
	{
		uint32_t sceneWidth;
		uint32_t sceneHeight;
		float minLog2Luminance;
		float log2LuminanceRange;
	};

	// Константный буфер CS b4, раскладка как у AdaptBuffer в Shaders/exposure_adapt.cs
	struct alignas( 16 ) AdaptParameters
	{
		int32_t meteringMode;
		float manualEV100;
		float exposureCompensation;
		float deltaTime;
		float minEV100;
		float maxEV100;
		float lowPercent;
		float highPercent;
		float speedUp;
		float speedDown;
		float minLog2Luminance;
		float log2LuminanceRange;
		int32_t curveKeyCount;
		float adaptPadding[3];
		XMFLOAT4 curveKeys[maxCurveKeys / 2];	// по два ключа (EV100, EV) на float4
	};

	// Константный буфер PS b2, раскладка как у PostProcessBuffer в Shaders/tonemap.ps
	struct alignas( 16 ) Parameters
	{
		int32_t tonemapper;
		float bloomScale;	// Bloom Intensity / сумма весов уровней
		float purkinjeShift;
		float padding;
	};

	// Константный буфер PS b2 проходов bloom, раскладка как у BloomBuffer в Shaders/bloom_*.ps
	struct alignas( 16 ) BloomParameters
	{
		XMFLOAT2 sourceTexelSize;
		float threshold;
		int32_t firstPass;
		float radius;
		float weight;
		float padding[2];
	};

	bool createExposureResources( float initialEV100, float exposureCompensation );
	bool createBloomTargets();
	void renderExposure( const ShaderView& sceneColor, float deltaTime );
	void readBackExposure();
	void renderBloom( const ShaderView& sceneColor, float threshold );
	void drawPass( const char* name, FullscreenShader& shader, const RenderTarget& target, const ShaderView& source,
				   BloomParameters params, BlendState blend );

	FullscreenShader m_shader;
	FullscreenShader m_bloomDownsample;
	FullscreenShader m_bloomUpsample;
	DMComputeShader m_histogramShader;
	DMComputeShader m_adaptShader;
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
