#pragma once

#include <cstdint>
#include <functional>
#include "D3D\GpuResources.h"
#include "RenderView.h"

namespace GS
{

// Съёмка сцены с другой камеры — как SceneCapture2D / SceneCaptureCube в UE: вид и свои цели. Рисуется в конце кадра, после
// проходов главного вида (тени, небо и освещение окружением кадра уже готовы): opaque без depth prepass и небо — без воды,
// частиц, облаков и кольцевых слоёв расстановки (трава); затенения окружением по экрану и объёма тумана нет (туман — по
// формуле), тени — только дальние (каскады подогнаны под главную камеру), объём воздушной перспективы — заново под вид
// (SceneObject::beginCaptureView). Заказ — Renderer::requestCapture или объект сцены сам (SceneObject::requestCapture: карта
// отражений места). docs/scene_capture.md
struct SceneCapture
{
	// Цвет очистки целей съёмки — он же optimized clear value их текстуры (SceneTargets::create, CubeTarget::create): с небом —
	// почти чёрный линейный, как у буфера сцены; без неба — прозрачный чёрный: альфа 0 там, где сцены нет
	static constexpr float clearColor[4] = { 0.004f, 0.004f, 0.004f, 1.0f };
	static constexpr float transparentClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	RenderView view;					// положение, матрицы, frustum; kind и index ставит рендерер
	const TargetView* color = nullptr;	// SceneTargets::colorFormat (R16G16B16A16_FLOAT) width × height
	const TargetView* depth = nullptr;	// SceneTargets::depthFormat (D32_FLOAT) того же размера
	uint32_t width = 0;
	uint32_t height = 0;
	// Яркость, кд/м², × это — в цель (preExposure съёмки); 0 — экспозиция главного вида (её ставит рендерер, done её видит)
	float exposure = 0.0f;
	// Фон неба (проход sky), как show flag Atmosphere у SceneCapture в UE. Без него цель очищается прозрачным чёрным
	// (transparentClearColor) — съёмка карты отражений: небо в отражении берётся по направлению луча, без поправки параллакса
	bool sky = true;
	// Цвет очистки этой съёмки
	const float* clear() const { return sky ? clearColor : transparentClearColor; }
	// После записи команд съёмки (в кадре): копия в кубическую карту, чтение на CPU; capture — с экспозицией съёмки
	std::function<void( const SceneCapture& capture )> done;
};

}
