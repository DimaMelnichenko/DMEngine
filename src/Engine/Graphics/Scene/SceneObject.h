#pragma once

#include <string>
#include "RenderView.h"
#include "MeshBatch.h"
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
	XMFLOAT3 toShadowLight;	// направление на источник теней: солнце, ночью — луна (DMLightDriver::shadowLight); y ≤ 0 — теней нет
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
	virtual PropertyContainer* properties() { return nullptr; }

	const std::string& name() const { return m_name; }
	bool visible() const { return m_visible; }
	void setVisible( bool visible ) { m_visible = visible; }

private:
	std::string m_name;
	bool m_visible = true;
};

}
