#pragma once

#include <string>
#include "RenderView.h"
#include "MeshBatch.h"
#include "D3D\RenderState.h"
#include "D3D\GpuPipeline.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

class ConstantBuffers;
class VertexPool;

// Данные кадра, общие для всех объектов сцены: главный вид (камера, frustum) и время
struct FrameContext
{
	const RenderView& view;
	float elapsedTime;		// длительность кадра, мс (с timestep — фиксированная)
	float gameTime;			// время игры, с: сумма длительностей кадров с запуска
	// Направление на солнце, нормированное; y ≤ 0 — солнце ниже горизонта или его нет: теней нет
	DirectX::XMFLOAT3 toShadowLight;	// направление на источник теней: солнце, ночью — луна (DMLightDriver::shadowLight); y ≤ 0 — теней нет
	// Все виды кадра для compute (по RenderView::index: 0 — главный, 1… — каскады теней), заполняет Renderer перед
	// compute(); 0 — только главный вид (view)
	const RenderView* views[maxRenderViews] = {};
	uint32_t viewCount = 0;
};

// Что рендерер передаёт объекту в его собственный вызов (CustomBatch): вид, проход, состояние растеризатора кадра
// (каркас по клавише Q) и общие данные конвейера — вместо глобальных синглтонов
struct RenderContext
{
	const RenderView& view;
	MeshPass pass;
	RasterState frameRaster;
	ConstantBuffers& constants;
	VertexPool& vertexPool;
	// Глубина вида уже записана depth prepass: непрозрачные проверяют её на равенство без записи, и Masked в проходе
	// цвета не отсекает по альфе и дизерингом — маска уже в глубине (ShaderPhaseOptions::depthFromPrepass)
	bool depthFromPrepass = false;
};

// Состояния и цели проходов рендерера, которых объект сам не знает, — для прогрева пайплайнов его своих вызовов
// (SceneObject::warmPipelines): пайплайн, собранный в кадре, — «ленивый» (фриз и строка в лог). Растеризатор кадра
// (сплошной / каркасный — RenderContext::frameRaster) и глубину проходов цвета объект знает по своим вызовам
struct PassStates
{
	RenderState shadowDepth;	// глубина каскадов теней: растеризатор теней со смещением глубины солнца уровня
	TargetFormats scene;		// буфер сцены (HDR-цвет + глубина) — проходы цвета
	TargetFormats depthOnly;	// только глубина — depth prepass и каскады теней
};

// Общий интерфейс объектов сцены. Scene вызывает update() на CPU, Renderer — compute() до отрисовки,
// collectMeshes() за каждый вид и renderCustom() в проходах своих вызовов. Объект не знает, в каком проходе рисуются
// его меши: их раскладывает рендерер (как mesh draw commands в UE), поэтому новый проход или вид (тени, depth
// prepass) не требует правки объектов
class SceneObject
{
public:
	explicit SceneObject( const std::string& name ) : m_name( name ) {}
	virtual ~SceneObject() = default;

	virtual void update( const FrameContext& frame ) {}
	// Вызывается каждый кадр независимо от видимости
	virtual void compute( const FrameContext& frame ) {}
	// Что объект рисует с вида: меши (MeshBatch) и / или свой вызов (CustomBatch) — как GetDynamicMeshElements в UE.
	// Невидимого объекта рендерер не спрашивает
	virtual void collectMeshes( const RenderView& view, MeshCollector& collector ) = 0;
	// Свой вызов в проходе context.pass — у объектов с нестандартной геометрией (террейн, расстановка, небо, частицы).
	// Перед ним привязан общий буфер вершин (VertexPool) с топологией TRIANGLELIST; свои буферы объект привязывает сам
	virtual void renderCustom( const RenderContext& context ) {}
	// Пайплайны своих вызовов для состояний проходов рендерера — при загрузке уровня и когда состояние прохода меняется
	// (смещение теней в GUI): Renderer::warmPipelines. Объекты, которые рисуют только меши материалов, ничего не делают
	virtual void warmPipelines( const PassStates& states ) {}
	virtual PropertyContainer* properties() { return nullptr; }

	const std::string& name() const { return m_name; }
	bool visible() const { return m_visible; }
	void setVisible( bool visible ) { m_visible = visible; }

private:
	std::string m_name;
	bool m_visible = true;
};

}
