#pragma once

#include <string>
#include <vector>
#include "SceneObject.h"
#include "Shaders\FullscreenShader.h"
#include "Shaders\DMComputeShader.h"
#include "Properties\PropertyContainer.h"

class DMLightDriver;

namespace GS
{

// Процедурное небо и освещение окружением от него (как Sky Atmosphere + Sky Light в UE, только проще).
// Когда меняется солнце (первый направленный источник Scene\Lights.ini) или настройки неба, compute() заново:
// - считает таблицу многократного рассеяния (Shaders/sky_multiscattering.ps, модель Hillaire 2020 как в UE5);
// - рендерит cubemap неба (Рэлей, Ми, озон, многократное рассеяние — Shaders/atmosphere.sh) и строит его мипы;
// - проецирует его на сферические гармоники — рассеянный свет (Shaders/sky_irradiance.cs);
// - префильтрует отражения по шероховатости GGX (Shaders/sky_prefilter.ps);
// а таблицу BRDF (Shaders/brdf_lut.ps) считает один раз. Затем привязывает всё это к слотам PS t101…t103
// (Shaders/ibl.sh). Свой вызов в проходе sky рисует небо фоном кадра. Настройки — [Sky] в Scene\Lights.ini и окно GUI
class SkyAtmosphere : public SceneObject
{
public:
	static constexpr uint32_t skySize = 256;			// грань cubemap неба (фон и источник IBL)
	static constexpr uint32_t specularSize = 128;		// мип 0 префильтра отражений
	static constexpr uint32_t specularMipCount = 6;	// = specularMipCount в Shaders/ibl.sh
	static constexpr uint32_t brdfLutSize = 128;
	static constexpr uint32_t irradianceSourceMip = 3;	// мип неба для гармоник (32 × 32)
	static constexpr uint32_t multipleScatteringSize = 32;

	SkyAtmosphere();

	bool initialize( const DMLightDriver& lights, const std::string& settingsFile );
	// Фон не рисуется, если у уровня своя модель неба (SkySphere); освещение окружением остаётся
	void setBackgroundVisible( bool visible );

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	PropertyContainer* properties() override;

private:
	// Константный буфер PS b2, раскладка как у SkyParameters в Shaders/atmosphere.sh
	struct alignas( 16 ) Parameters
	{
		XMFLOAT3 sunDirection;
		float skyIntensity;
		XMFLOAT3 sunColor;
		float haze;
		XMFLOAT3 groundAlbedo;
		float roughness;
		int32_t face;
		float sourceSize;
		float padding[2];
	};

	bool createCube( uint32_t size, uint32_t mipCount, bool generateMips, com_unique_ptr<ID3D11Texture2D>& texture,
					 std::vector<com_unique_ptr<ID3D11RenderTargetView>>& targets, com_unique_ptr<ID3D11ShaderResourceView>& srv );
	bool createTexture2D( uint32_t size, DXGI_FORMAT format, com_unique_ptr<ID3D11Texture2D>& texture,
						  com_unique_ptr<ID3D11RenderTargetView>& target, com_unique_ptr<ID3D11ShaderResourceView>& srv );
	bool createIrradianceBuffer();
	Parameters currentParameters() const;
	void updateEnvironment( const Parameters& params );
	void setParameters( const Parameters& params );
	void bindEnvironment();

	const DMLightDriver* m_lights = nullptr;
	PropertyContainer m_properties;
	bool m_backgroundVisible = true;
	bool m_environmentValid = false;
	bool m_brdfReady = false;
	Parameters m_computedFor = {};

	FullscreenShader m_multipleScatteringShader;
	FullscreenShader m_cubeShader;
	FullscreenShader m_prefilterShader;
	FullscreenShader m_brdfShader;
	FullscreenShader m_backgroundShader;
	DMComputeShader m_irradianceShader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;

	com_unique_ptr<ID3D11Texture2D> m_multipleScattering;
	com_unique_ptr<ID3D11RenderTargetView> m_multipleScatteringTarget;
	com_unique_ptr<ID3D11ShaderResourceView> m_multipleScatteringSRV;

	com_unique_ptr<ID3D11Texture2D> m_skyCube;
	std::vector<com_unique_ptr<ID3D11RenderTargetView>> m_skyTargets;			// грань × мип
	com_unique_ptr<ID3D11ShaderResourceView> m_skySRV;
	com_unique_ptr<ID3D11ShaderResourceView> m_skyFacesSRV;					// мип irradianceSourceMip как массив граней

	com_unique_ptr<ID3D11Texture2D> m_specularCube;
	std::vector<com_unique_ptr<ID3D11RenderTargetView>> m_specularTargets;	// грань × мип
	com_unique_ptr<ID3D11ShaderResourceView> m_specularSRV;

	com_unique_ptr<ID3D11Texture2D> m_brdfLut;
	com_unique_ptr<ID3D11RenderTargetView> m_brdfTarget;
	com_unique_ptr<ID3D11ShaderResourceView> m_brdfSRV;

	com_unique_ptr<ID3D11Buffer> m_irradianceBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_irradianceUAV;
	com_unique_ptr<ID3D11ShaderResourceView> m_irradianceSRV;
};

}
