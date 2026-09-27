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
// Результата два: bind() привязывает готовый, пересчёт пишет в другой, и они меняются, когда пересчёт закончен
// целиком — освещение окружением не бывает наполовину новым
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

	bool initialize();
	// Источник того же формата, что создаёт createSource(): полная цепочка мипов и мип irradianceSourceMip как массив
	// граней. Весь пересчёт сразу — панорама и первый кадр атмосферы
	void capture( const CubeTarget& source );
	// Пересчёт по шагу за кадр, как Real Time Capture с time slicing у Sky Light в UE: beginCapture запоминает
	// источник (до конца пересчёта его нельзя менять), updateCapture делает следующий шаг и возвращает true, когда
	// результат готов и подменил прежний
	void beginCapture( const CubeTarget& source );
	bool updateCapture();
	bool capturing() const { return m_source != nullptr; }
	void bind();

	// Cubemap источника sourceSize² с мипами (GenerateMips) и видом граней для гармоник
	static bool createSource( CubeTarget& source );

private:
	// Константный буфер PS b2, раскладка как у PrefilterParameters в Shaders/sky_prefilter.ps
	struct alignas( 16 ) PrefilterParameters
	{
		int32_t face;
		float roughness;
		float sourceSize;
		float padding;
	};

	// Результат пересчёта: префильтр отражений и гармоники рассеянного света
	struct Result
	{
		CubeTarget specular;
		com_unique_ptr<ID3D11Buffer> irradianceBuffer;
		com_unique_ptr<ID3D11UnorderedAccessView> irradianceUAV;
		com_unique_ptr<ID3D11ShaderResourceView> irradianceSRV;
	};

	static bool createResult( Result& result );
	void captureIrradiance( Result& result );
	void prefilterFace( Result& result, int32_t face );

	FullscreenShader m_prefilterShader;
	FullscreenShader m_brdfShader;
	DMComputeShader m_irradianceShader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	bool m_brdfReady = false;

	Result m_results[2];
	uint32_t m_front = 0;					// готовый результат, который привязывает bind()
	const CubeTarget* m_source = nullptr;	// идёт пересчёт из этого источника в m_results[1 − m_front]
	uint32_t m_step = 0;
	RenderTarget m_brdfLut;
};

}
