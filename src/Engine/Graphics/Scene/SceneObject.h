#pragma once

#include <string>
#include "Camera\DMCamera.h"
#include "Camera\DMFrustum.h"
#include "Properties/PropertyContainer.h"
#include "Shaders\MaterialRenderState.h"

namespace GS
{

// Данные кадра, общие для всех объектов сцены
struct FrameContext
{
	const DMCamera& camera;
	DMFrustum frustum;
	float elapsedTime;
};

// Проход рендерера, в котором рисуется объект
enum class RenderPass
{
	sky,			// первым, объект сам отключает глубину
	opaque,			// непрозрачные и с отсечением по альфе (Opaque и Masked)
	transparent		// после непрозрачных: альфа-блендинг, глубина только читается (Translucent)
};

// Проход для режима материала
inline RenderPass passFor( BlendMode mode )
{
	return mode == BlendMode::translucent ? RenderPass::transparent : RenderPass::opaque;
}

// Общий интерфейс объектов сцены. Scene вызывает update() на CPU, Renderer — compute() до отрисовки
// и render() в каждом проходе, где объект рисует (drawsIn). Перед render() рендерер привязывает общий буфер вершин
// и индексов (VertexPool) с топологией TRIANGLELIST; объект со своими буферами привязывает их сам
class SceneObject
{
public:
	SceneObject( const std::string& name, RenderPass pass ) : m_name( name ), m_pass( pass ) {}
	virtual ~SceneObject() = default;

	virtual void update( const FrameContext& frame ) {}
	// Вызывается каждый кадр независимо от видимости
	virtual void compute( const FrameContext& frame ) {}
	virtual void render( const FrameContext& frame, RenderPass pass ) = 0;
	virtual PropertyContainer* properties() { return nullptr; }

	// Рисует ли объект в проходе. По умолчанию — только в своём; объект с материалами разных режимов (модели,
	// расстановка) рисует непрозрачные в opaque, а полупрозрачные — в transparent
	virtual bool drawsIn( RenderPass pass ) const { return pass == m_pass; }

	const std::string& name() const { return m_name; }
	RenderPass pass() const { return m_pass; }
	bool visible() const { return m_visible; }
	void setVisible( bool visible ) { m_visible = visible; }

private:
	std::string m_name;
	RenderPass m_pass;
	bool m_visible = true;
};

}
