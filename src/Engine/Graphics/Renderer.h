#pragma once

#include <string>
#include "Scene\VertexPool.h"
#include "D3D\DMSamplerState.h"
#include "SceneObject.h"

class GUI;

namespace GS
{

class Scene;

// Отправляет на GPU кадр сцены: общие данные конвейера, compute-проходы объектов,
// затем проходы отрисовки sky → opaque → transparent. Время каждого объекта выводится в GUI
class Renderer
{
public:
	explicit Renderer( GUI& gui );

	// Вызывается после загрузки мешей: собирает общий буфер вершин и индексов
	bool initialize();

	// Рисует сцену в back buffer между DMD3D::BeginScene и отрисовкой GUI
	void render( Scene& scene, const FrameContext& frame, bool wireframe );

private:
	void preparePipeline( Scene& scene, const FrameContext& frame );
	void renderPass( Scene& scene, const FrameContext& frame, RenderPass pass );

	template<typename Func>
	void measure( const std::string& counterName, Func&& func );

private:
	GUI& m_gui;
	VertexPool m_vertexPool;
	DMSamplerState m_samplerState;
};

}
