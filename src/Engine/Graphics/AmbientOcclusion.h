#pragma once

#include "DirectX.h"
#include "D3D\GpuResources.h"
#include "ComputeShader.h"
#include "RenderView.h"
#include "Level\LevelSettings.h"
#include "Shaders\ambient_occlusion_constants.h"
#include "RenderFeature.h"

namespace GS
{

// Экранное затенение окружением — GTAO (Jimenez 2016; как XeGTAO и GTAO в UE5): по глубине depth prepass в половине
// кадра, до прохода цвета (Shaders/gtao.cs: расчёт и фильтр по глубине). Результат — ресурс сцены
// SLOT_AMBIENT_OCCLUSION (t126): видимость и глубина точки; материалы берут его в пикселе
// (Shaders/ambient_occlusion.sh) — гасит только освещение окружением (Surface::screenOcclusion). Даёт то, чего нет в
// текстурах: землю под травой, камни друг на друге, стволы на рельефе. Настройки — у постобработки, как Ambient
// Occlusion в Post Process Volume UE (PostProcessSettings: сила, радиус, степень), окно «Post process»
class AmbientOcclusion : public RenderFeature
{
public:
	using Settings = AmbientOcclusionSettings;

	bool initialize();
	// Новый размер кадра: половина заднего буфера
	bool resize() override;
	// Настройки кадра — из постобработки (PostProcessSettings::ambientOcclusion), до beginFrame; show — «Show ambient
	// occlusion», отладочный вид: кадр — само затенение
	void setSettings( const Settings& settings, bool show );
	// Затенение в этом кадре: включено (сила > 0) и есть depth prepass — по его глубине; cb_ambientOcclusion: x — сила
	// (0 — затенения нет), yz — размер в текселях, w — 1: отладочный вид
	void beginFrame( const RenderFrame& frame, FrameParameters& constants ) override;
	bool active() const { return m_active; }
	// После depth prepass, до прохода цвета: расчёт и фильтр, привязка результата (t126) проходам сцены
	void render( const RenderView& view, const ShaderView& sceneDepth );

private:
	Settings m_settings;
	bool m_show = false;
	bool m_active = false;

	ComputeShader m_shader;
	ComputeShader m_denoiseShader;
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
