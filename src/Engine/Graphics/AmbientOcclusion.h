#pragma once

#include "DirectX.h"
#include "D3D\GpuResources.h"
#include "DMComputeShader.h"
#include "RenderView.h"
#include "Level\LevelSettings.h"

namespace GS
{

// Экранное затенение окружением — GTAO (Jimenez 2016; как XeGTAO и GTAO в UE5): по глубине depth prepass в половине
// кадра, до прохода цвета (Shaders/gtao.cs: расчёт и фильтр по глубине). Результат — ресурс сцены
// SLOT_AMBIENT_OCCLUSION (t126): видимость и глубина точки; материалы берут его в пикселе
// (Shaders/ambient_occlusion.sh) — гасит только освещение окружением (Surface::screenOcclusion). Даёт то, чего нет в
// текстурах: землю под травой, камни друг на друге, стволы на рельефе. Настройки — у постобработки, как Ambient
// Occlusion в Post Process Volume UE (PostProcessSettings: сила, радиус, степень), окно «Post process»
class AmbientOcclusion
{
public:
	using Settings = AmbientOcclusionSettings;

	bool initialize();
	// Новый размер кадра: половина заднего буфера
	bool resize();
	// После depth prepass, до прохода цвета: расчёт и фильтр, привязка результата (t126) проходам сцены
	void render( const RenderView& view, const ShaderView& sceneDepth, const Settings& settings );
	// cb_ambientOcclusion кадра: x — сила (0 — затенения нет), yz — размер в текселях, w — 1: кадр — само затенение
	// («Show ambient occlusion», отладочный вид)
	DirectX::XMFLOAT4 frameParameters( const Settings& settings, bool active, bool show ) const;

private:
	// cbuffer AmbientOcclusionBuffer в Shaders/gtao.cs
	struct alignas( 16 ) Parameters
	{
		DirectX::XMFLOAT2 tanHalfFov;
		DirectX::XMFLOAT2 depthLinearize;
		uint32_t fullSize[2];
		uint32_t halfSize[2];
		float radius;
		float falloffMul;
		float falloffAdd;
		float pixelsPerMeter;
		float finalPower;
		float maxPixelRadius;
		float depthTolerance;
		float padding;
	};
	static_assert( sizeof( Parameters ) == 64, "AmbientOcclusionBuffer layout" );

	DMComputeShader m_shader;
	DMComputeShader m_denoiseShader;
	Buffer m_constants;
	// Расчёт (с шумом) и после фильтра: R16G16_FLOAT — видимость и глубина вида, м
	Texture m_noisy;
	Texture m_result;
	ShaderView m_noisySRV;
	StorageView m_noisyUAV;
	ShaderView m_resultSRV;
	StorageView m_resultUAV;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
};

}
