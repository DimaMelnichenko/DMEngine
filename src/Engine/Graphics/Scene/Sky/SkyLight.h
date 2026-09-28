#pragma once

#include "Shaders\FullscreenShader.h"
#include "Shaders\DMComputeShader.h"
#include "D3D\CubeTarget.h"
#include "D3D\RenderTarget.h"

namespace GS
{

// Освещение окружением из cubemap окружения (как Sky Light в UE): рассеянный свет — сферические гармоники
// (Shaders/sky_irradiance.cs), отражения — cubemap, префильтрованный по шероховатости GGX (Shaders/sky_prefilter.ps),
// и таблица BRDF для split-sum (Shaders/brdf_lut.ps, один раз). Источник — небо атмосферы (SkyAtmosphere) или
// панорама (HDRIBackdrop): тот, кто его рисует, при изменении зовёт capture() или beginCapture(), а bind() — каждый
// кадр (слоты PS t101…t103, Shaders/ibl.sh). Масштаб яркости — константа кадра cb_skyLightScale.
// Результатов три: прежний, текущий (последний готовый) и тот, куда идёт пересчёт; результат не бывает наполовину
// новым. После каждого пересчёта свет переходит от прежнего к текущему за blendFrames кадров (смесь — в четвёртый,
// показываемый результат: Shaders/sky_light_blend.ps и sky_irradiance_blend.cs), так что при быстром времени суток
// освещение окружением меняется плавно, а не ступеньками раз в пересчёт; отстаёт оно от неба на два пересчёта.
// Результат хранит свет, делённый на нормировку пересчёта (оценка яркости неба: ночью ~10⁻⁶…10⁻⁹ в единицах
// запекания), поэтому префильтр — в половинной точности; шейдеры умножают на cb_skyLightScale с нормировкой показанного
class SkyLight
{
public:
	static constexpr uint32_t sourceSize = 128;			// грань cubemap источника (= specularSize): мип 0 — отражения без размытия, мипы — префильтр
	static constexpr uint32_t irradianceSourceMip = 2;	// мип источника для гармоник (32 × 32)
	static constexpr uint32_t specularSize = 128;		// мип 0 префильтра отражений
	static constexpr uint32_t specularMipCount = 6;	// = specularMipCount в Shaders/ibl.sh
	static constexpr uint32_t brdfLutSize = 128;
	// Шаги пересчёта: гармоники, затем префильтр по грани (все мипы грани за шаг)
	static constexpr uint32_t captureSteps = 7;
	// Кадров перехода от прежнего результата к новому — столько же, сколько идёт следующий пересчёт с кадром
	// cubemap неба (SkyAtmosphere): при солнце в движении переход кончается, когда готов следующий
	static constexpr uint32_t blendFrames = captureSteps + 1;

	bool initialize();
	// Источник того же формата, что создаёт createSource(): полная цепочка мипов и мип irradianceSourceMip как массив
	// граней. Весь пересчёт сразу — панорама и первый кадр атмосферы
	// normalization — делитель результата (яркость неба в единицах источника); 1 — как есть (панорама)
	void capture( const CubeTarget& source, float normalization = 1.0f );
	// Пересчёт по шагу за кадр, как Real Time Capture с time slicing у Sky Light в UE: beginCapture запоминает
	// источник (до конца пересчёта его нельзя менять), updateCapture делает следующий шаг и возвращает true, когда
	// результат готов и подменил прежний
	void beginCapture( const CubeTarget& source, float normalization = 1.0f );
	bool updateCapture();
	bool capturing() const { return m_source != nullptr; }
	// Раз за кадр: освещение окружением в слоты PS; во время перехода — сначала смесь прежнего и текущего
	void bind();
	// Нормировка показанного результата: на неё умножают освещение окружением (cb_skyLightScale)
	float normalization() const { return m_results[m_current].normalization; }

	// Cubemap источника sourceSize² с мипами (GenerateMips) и видом граней для гармоник
	static bool createSource( CubeTarget& source );

private:
	// Константный буфер PS b2, раскладка как у PrefilterParameters в Shaders/sky_prefilter.ps
	struct alignas( 16 ) PrefilterParameters
	{
		int32_t face;
		float roughness;
		float sourceSize;
		float outputScale;	// 1 / нормировка
	};
	// Константный буфер PS b2 — BlendParameters в Shaders/sky_light_blend.ps; CS b4 — sky_irradiance_blend.cs:
	// результат = прежний × previousWeight + текущий × currentWeight (веса учитывают разные нормировки)
	struct alignas( 16 ) BlendParameters
	{
		int32_t face;
		float mip;
		float previousWeight;
		float currentWeight;
	};

	// Результат пересчёта: префильтр отражений и гармоники рассеянного света
	struct Result
	{
		CubeTarget specular;
		com_unique_ptr<ID3D11Buffer> irradianceBuffer;
		com_unique_ptr<ID3D11UnorderedAccessView> irradianceUAV;
		com_unique_ptr<ID3D11ShaderResourceView> irradianceSRV;
		float normalization = 1.0f;	// результат — свет, делённый на неё
	};

	static bool createResult( Result& result );
	void captureIrradiance( Result& result );
	void prefilterFace( Result& result, int32_t face );
	// Смесь m_results[m_previous] и m_results[m_current] с долей blend — в m_blended
	void blendResults( float blend );

	FullscreenShader m_prefilterShader;
	FullscreenShader m_brdfShader;
	FullscreenShader m_blendShader;
	DMComputeShader m_irradianceShader;
	DMComputeShader m_irradianceBlendShader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	com_unique_ptr<ID3D11Buffer> m_blendConstants;
	com_unique_ptr<ID3D11Buffer> m_irradianceBlendConstants;
	com_unique_ptr<ID3D11Buffer> m_irradianceConstants;	// CS b4 гармоник: 1 / нормировка
	bool m_brdfReady = false;

	Result m_results[3];
	Result m_blended;						// показываемый результат во время перехода
	uint32_t m_previous = 0;				// от него идёт переход
	uint32_t m_current = 0;					// последний готовый
	uint32_t m_building = 1;				// сюда идёт пересчёт
	uint32_t m_blendFrame = blendFrames;	// кадров с готовности m_current; с blendFrames — только он
	const CubeTarget* m_source = nullptr;	// идёт пересчёт из этого источника в m_results[m_building]
	uint32_t m_step = 0;
	float m_captureNormalization = 1.0f;	// нормировка идущего пересчёта
	RenderTarget m_brdfLut;
};

}
