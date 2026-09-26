#include "Renderer.h"
#include <chrono>
#include "Scene.h"
#include "Pipeline.h"
#include "GUI\GUI.h"
#include "D3D\DMD3D.h"

namespace GS
{

Renderer::Renderer( GUI& gui ) :
	m_gui( gui )
{
}

template<typename Func>
void Renderer::measure( const std::string& counterName, Func&& func )
{
	auto start = std::chrono::high_resolution_clock::now();
	func();
	auto end = std::chrono::high_resolution_clock::now();
	m_gui.addCounterInfo( counterName + " = %.3f ms", std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0f );
}

bool Renderer::initialize()
{
	if( !m_vertexPool.prepareMeshes() )
		return false;

	if( !m_postProcess.initialize( "Scene\\Lights.ini" ) )
		return false;

	return m_samplerState.initialize();
}

void Renderer::render( Scene& scene, const FrameContext& frame, bool wireframe )
{
	measure( "preparePipeline", [&] { preparePipeline( scene, frame ); } );

	measure( "Compute Pass", [&]
	{
		for( SceneObject* object : scene.objects() )
		{
			object->compute( frame );
		}
	} );

	DMD3D::instance().BeginScene( 0.004f, 0.004f, 0.004f, 1.0f );

	// Базовое состояние кадра; объекты меняют его только в своей области видимости
	ScopedRenderState frameState( wireframe ? RasterState::wireframe : RasterState::solid );

	renderPass( scene, frame, RenderPass::sky );
	renderPass( scene, frame, RenderPass::opaque );

	{
		ScopedRenderState blendState( BlendState::alpha );
		renderPass( scene, frame, RenderPass::transparent );
	}

	measure( "Post process", [&] { m_postProcess.render(); } );
}

PropertyContainer* Renderer::postProcessProperties()
{
	return m_postProcess.properties();
}

void Renderer::preparePipeline( Scene& scene, const FrameContext& frame )
{
	m_samplerState.setDefaultSmaplers();
	// установка источников света
	int lightCount = scene.lights().setBuffer( 100, SRVType::ps );
	// установка матриц в шейдер константы
	pipeline().shaderConstant().setPerFrameBuffer( frame.camera, lightCount,
												   scene.lights().ambientSkyColor(), scene.lights().ambientGroundColor() );
}

void Renderer::renderPass( Scene& scene, const FrameContext& frame, RenderPass pass )
{
	for( SceneObject* object : scene.objects() )
	{
		if( object->pass() != pass || !object->visible() )
			continue;

		// Объект рисует из общего буфера или привязывает свои буферы сам
		m_vertexPool.setBuffers();
		measure( object->name(), [&] { object->render( frame ); } );
	}
}

}
