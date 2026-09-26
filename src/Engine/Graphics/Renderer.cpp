#include "Renderer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include "Scene.h"
#include "Pipeline.h"
#include "GUI\GUI.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

namespace GS
{

Renderer::Renderer( GUI& gui ) :
	m_gui( gui )
{
}

template<typename Func>
void Renderer::measure( const std::string& counterName, Func&& func )
{
	m_gpuProfiler.beginScope( counterName );
	auto start = std::chrono::high_resolution_clock::now();
	func();
	auto end = std::chrono::high_resolution_clock::now();
	m_gpuProfiler.endScope();
	m_gui.addCounterInfo( counterName + " = %.3f ms", std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0f );
}

bool Renderer::initialize()
{
	if( !m_vertexPool.prepareMeshes() )
		return false;

	if( !m_postProcess.initialize( "Scene\\Lights.ini" ) )
		return false;

	if( !m_gpuProfiler.initialize( DMD3D::instance().GetDevice(), DMD3D::instance().GetDeviceContext() ) )
		return false;

	return m_samplerState.initialize();
}

void Renderer::render( Scene& scene, const FrameContext& frame, bool wireframe )
{
	m_gpuProfiler.beginFrame();
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
		// Полупрозрачные не пишут глубину: иначе закрыли бы то, что за ними рисуется позже
		ScopedRenderState transparentState( BlendState::alpha, DepthState::readOnly );
		renderPass( scene, frame, RenderPass::transparent );
	}

	measure( "Post process", [&] { m_postProcess.render(); } );

	m_gpuProfiler.endFrame();
	reportGpuTimes();
}

void Renderer::reportGpuTimes()
{
	// Время GPU отстаёт от кадра на несколько кадров: запросы читаются без ожидания
	m_gui.addCounterInfo( "GPU frame = %.3f ms", m_gpuProfiler.frameMilliseconds() );
	for( const auto& [name, milliseconds] : m_gpuProfiler.results() )
		m_gui.addCounterInfo( "GPU " + name + " = %.3f ms", milliseconds );

	// Прогрев — первые кадры (пересчёт окружения неба, заполнение очередей) в среднее не входят
	constexpr uint32_t warmupFrames = 60;
	constexpr auto averageDuration = std::chrono::seconds( 3 );
	if( m_gpuAverageLogged || ++m_frameIndex <= warmupFrames || m_gpuProfiler.results().empty() )
		return;

	if( m_gpuAverages.empty() )
	{
		m_gpuAverageStart = std::chrono::steady_clock::now();
		m_gpuAverages.push_back( { "frame" } );
	}
	m_gpuAverages[0].sum += m_gpuProfiler.frameMilliseconds();
	m_gpuAverages[0].count++;
	for( const auto& [name, milliseconds] : m_gpuProfiler.results() )
	{
		auto it = std::find_if( m_gpuAverages.begin(), m_gpuAverages.end(), [&]( const GpuAverage& a ) { return a.name == name; } );
		if( it == m_gpuAverages.end() )
		{
			m_gpuAverages.push_back( { name } );
			it = m_gpuAverages.end() - 1;
		}
		it->sum += milliseconds;
		it->count++;
	}

	if( std::chrono::steady_clock::now() - m_gpuAverageStart < averageDuration )
		return;

	char value[32];
	std::string line = "GPU average over " + std::to_string( m_gpuAverages[0].count ) + " frames, ms:";
	for( const GpuAverage& average : m_gpuAverages )
	{
		std::snprintf( value, sizeof( value ), "%.3f", average.sum / std::max( average.count, 1u ) );
		line += " " + average.name + " " + value + ";";
	}
	LOG( line );
	m_gpuAverageLogged = true;
}

PropertyContainer* Renderer::postProcessProperties()
{
	return m_postProcess.properties();
}

void Renderer::preparePipeline( Scene& scene, const FrameContext& frame )
{
	m_samplerState.setDefaultSmaplers();
	// установка источников света
	int lightCount = scene.lights().setBuffer( SLOT_LIGHTS, SRVType::ps );
	// установка матриц в шейдер константы
	pipeline().shaderConstant().setPerFrameBuffer( frame.camera, lightCount );
}

void Renderer::renderPass( Scene& scene, const FrameContext& frame, RenderPass pass )
{
	// Проход не полагается на состояние, оставленное прошлым: своя цель, область вывода, без чужих ресурсов.
	// Растеризатор, глубину и блендинг задаёт ScopedRenderState кадра и прохода
	DMD3D::instance().setSceneTarget();
	DMD3D::instance().unbindTransientResources();

	static const char* const passNames[] = { "Pass sky", "Pass opaque", "Pass transparent" };
	m_gpuProfiler.beginScope( passNames[static_cast<int>( pass )] );

	for( SceneObject* object : scene.objects() )
	{
		if( !object->drawsIn( pass ) || !object->visible() )
			continue;

		// Объект рисует из общего буфера или привязывает свои буферы сам
		m_vertexPool.setBuffers();
		measure( object->name(), [&] { object->render( frame, pass ); } );
	}

	m_gpuProfiler.endScope();
}

}
