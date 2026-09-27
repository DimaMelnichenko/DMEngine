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
// панорама (HDRIBackdrop): тот, кто его рисует, зовёт capture() при изменении, а bind() — каждый кадр (слоты PS
// t101…t103, Shaders/ibl.sh). Масштаб яркости — константа кадра cb_skyLightScale
class SkyLight
{
public:
	static constexpr uint32_t sourceSize = 256;			// грань cubemap источника: мип 0 — фон, мипы — префильтр
	static constexpr uint32_t irradianceSourceMip = 3;	// мип источника для гармоник (32 × 32)
	static constexpr uint32_t specularSize = 128;		// мип 0 префильтра отражений
	static constexpr uint32_t specularMipCount = 6;	// = specularMipCount в Shaders/ibl.sh
	static constexpr uint32_t brdfLutSize = 128;

	bool initialize();
	// Источник того же формата, что создаёт createSource(): полная цепочка мипов и мип irradianceSourceMip как массив
	// граней. Снимает свои ресурсы со слотов PS перед пересчётом
	void capture( const CubeTarget& source );
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

	bool createIrradianceBuffer();

	FullscreenShader m_prefilterShader;
	FullscreenShader m_brdfShader;
	DMComputeShader m_irradianceShader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	bool m_brdfReady = false;

	CubeTarget m_specular;
	RenderTarget m_brdfLut;
	com_unique_ptr<ID3D11Buffer> m_irradianceBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_irradianceUAV;
	com_unique_ptr<ID3D11ShaderResourceView> m_irradianceSRV;
};

}
