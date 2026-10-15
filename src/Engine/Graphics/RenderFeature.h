#pragma once

#include "Scene\RenderView.h"
#include "Properties\PropertyContainer.h"

namespace GS
{

struct FrameParameters;

// Кадр для возможностей рендерера (RenderFeature::beginFrame): главный вид и то, что рендерер знает о кадре до его проходов
struct RenderFrame
{
	const RenderView& view;
	float ev100;			// экспозиция прошлого кадра (PostProcess): этот кадр её ещё не посчитал
	bool depthPrepass;		// в кадре есть depth prepass (флажок «Depth prepass» окна «Renderer»)
};

// Возможность рендерера — свой проход кадра со своими ресурсами (тени, туман, затенение окружением, постобработка), как
// renderer feature в Unity URP или проходы FSceneRenderer в UE. Renderer держит их списком (Renderer::features) и обходит
// в общих точках жизни кадра: начало кадра с вкладом в константы, новый размер кадра, смена плана; окна свойств по списку
// регистрирует DMGraphics. Сам проход рендерер вызывает в его месте кадра: проходы читают разное (карту теней, глубину
// prepass, цвет сцены) — порядок и ресурсы между ними задаст граф кадра (TODO, «Архитектура»)
class RenderFeature
{
public:
	virtual ~RenderFeature() = default;

	// Окно свойств в Outliner; nullptr — окна нет
	virtual PropertyContainer* properties() { return nullptr; }
	// Часть окружения уровня (раздел Environment, «Save level» пишет окно в базу); иначе — настройка рендеринга (Rendering)
	virtual bool environment() const { return false; }
	// Начало кадра, до compute объектов сцены: вклад в константы кадра (как SceneObject::frameConstants) и привязки, которые
	// читают проходы сцены
	virtual void beginFrame( const RenderFrame& frame, FrameParameters& constants ) {}
	// Новый размер кадра (WM_SIZE): ресурсы под размер заднего буфера заново
	virtual bool resize() { return true; }
	// Смена плана: история прошлых кадров (адаптация экспозиции, объём тумана) не годится
	virtual void cameraCut() {}
};

}
