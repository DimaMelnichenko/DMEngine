#pragma once

#include <cstdint>
#include "D3D\GpuResources.h"
#include "D3D\GpuPipeline.h"

namespace GS
{

// Буфер сцены — цели проходов кадра, владеет Renderer: HDR-цвет R16G16B16A16_FLOAT (яркость × экспозиция прошлого
// кадра, линейные значения без ограничения сверху) и обратная глубина D32_FLOAT (1 у ближней плоскости, 0 у дальней),
// размером с задний буфер. Глубина видна и шейдерам (SceneDepthTexture в UE): текстура R32_TYPELESS — цель D32_FLOAT,
// вид R32_FLOAT (depthView) и цель только для чтения (depthReadTarget) — для прохода, который читает её сам. Создаётся при инициализации рендерера и заново при смене размера окна (Renderer::resize);
// проходы сцены рисуют в colorTarget / depthTarget, постобработка читает цвет через colorView. Тонмаппинг переводит
// его в задний буфер (DMD3D::backBufferTarget)
class SceneTargets
{
public:
	static constexpr DXGI_FORMAT colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static constexpr DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;
	static constexpr DXGI_FORMAT depthTextureFormat = DXGI_FORMAT_R32_TYPELESS;
	static constexpr DXGI_FORMAT depthViewFormat = DXGI_FORMAT_R32_FLOAT;
	// Форматы целей для прогрева пайплайнов: проходы цвета (цвет + глубина) и только глубина (depth prepass и каскады
	// теней — формат глубины тот же, пайплайн один на оба)
	static TargetFormats formats();
	static TargetFormats depthOnlyFormats();

	// clearColor — цвет очистки, он же optimized clear value текстуры: очистка другим цветом медленнее, и debug-слой
	// предупреждает о ней
	bool create( uint32_t width, uint32_t height, const float clearColor[4] );
	// Проход очистки в начале кадра: цвет — clearColor создания, глубина — 0 (дальняя плоскость)
	void clear() const;

	const TargetView& colorTarget() const { return m_colorTarget; }
	const TargetView& depthTarget() const { return m_depthTarget; }
	// Глубина только для чтения: проход с ней проверяет глубину без записи и читает её шейдерами (depthView)
	const TargetView& depthReadTarget() const { return m_depthReadTarget; }
	const ShaderView& colorView() const { return m_colorView; }
	const ShaderView& depthView() const { return m_depthView; }
	uint32_t width() const { return m_width; }
	uint32_t height() const { return m_height; }

private:
	Texture m_color;
	Texture m_depth;
	TargetView m_colorTarget;
	TargetView m_depthTarget;
	TargetView m_depthReadTarget;
	ShaderView m_colorView;
	ShaderView m_depthView;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	float m_clearColor[4] = {};
};

}
