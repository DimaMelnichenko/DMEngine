#pragma once

#include <vector>
#include "D3D\GpuResources.h"
#include "D3D\GpuPass.h"
#include "D3D\GpuPipeline.h"
#include "ComputeShader.h"
#include "RenderFeature.h"

namespace GS
{

// Экранные отражения (как Screen Space Reflections в UE, но только у поверхностей, которые их просят; docs/screen_reflections.md).
// После непрозрачных и неба — пирамида глубины сцены (Hi-Z: ближайшая и дальняя глубина в ячейках 2^L пикселей, ресурс
// сцены SLOT_SCENE_DEPTH_PYRAMID); свои вызовы с битом прохода MeshPass::reflectionPrepass (вода) рисуют в цели этого
// прохода глубину поверхности и нормаль без ряби (surfacePass); затем из каждого их пикселя идёт отражённый луч по пирамиде
// (Shaders/screen_reflections.cs, screen_reflection_trace.sh). Результат — ресурс сцены SLOT_SCREEN_REFLECTION (t127) для
// прохода transparent: uv попадания, вес и доля пути луча в глубине попадания; цвет по ним материал берёт сам из копии
// цвета сцены (SLOT_SCENE_COLOR) — со сдвигом по своей ряби (Shaders/water_shading.sh, screenReflection), поэтому ни цвет,
// ни рябь в этот проход не входят
class ScreenSpaceReflections : public RenderFeature
{
public:
	static constexpr DXGI_FORMAT normalFormat = DXGI_FORMAT_R16G16_SNORM;
	static constexpr DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;
	// Цели прохода поверхностей — для прогрева пайплайнов (PassStates::reflectionSurface)
	static TargetFormats surfaceFormats();

	bool initialize();
	// Новый размер кадра: цели поверхностей, пирамида и результат — под размер заднего буфера
	bool resize() override;
	// Пирамида глубины сцены и её привязка (t128)
	void buildPyramid( const ShaderView& sceneDepth );
	// Объявление прохода поверхностей: его цели; clearSurfaces — очистка перед ним (глубина 0 — поверхности нет)
	PassDesc surfacePass() const;
	void clearSurfaces() const;
	// После прохода поверхностей и пирамиды: лучи и привязка результата (t127) проходу transparent
	void trace( const ShaderView& sceneDepth );

private:
	ComputeShader m_hizFirstShader;		// уровни 1…4
	ComputeShader m_hizRestShader;		// уровни 5…7
	ComputeShader m_traceShader;
	// Поверхности: обратная глубина (R32_TYPELESS — цель D32 и вид R32_FLOAT) и нормаль без ряби (x, z)
	Texture m_surfaceDepth;
	TargetView m_surfaceDepthTarget;
	ShaderView m_surfaceDepthView;
	Texture m_surfaceNormal;
	TargetView m_surfaceNormalTarget;
	ShaderView m_surfaceNormalView;
	// Пирамида глубины R32G32_FLOAT — ближайшая и дальняя глубина ячейки, уровни 1…7 (уровень 0 — сама глубина сцены):
	// стороны уровня 0 кратны 2^(уровней − 1) (за краем кадра — 0), вид всех уровней и по уровню
	Texture m_hiz;
	ShaderView m_hizView;
	std::vector<ShaderView> m_hizLevelViews;
	std::vector<StorageView> m_hizLevelUAVs;
	// Результат: R16G16B16A16_UNORM — uv попадания, вес, доля пути луча в глубине попадания
	Texture m_result;
	ShaderView m_resultView;
	StorageView m_resultUAV;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_hizWidth = 0;
	uint32_t m_hizHeight = 0;
};

}
