#include "Renderer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "Scene.h"
#include "Pipeline.h"
#include "GUI\GUI.h"
#include "D3D\DMD3D.h"
#include "Shaders\DMShader.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

// Один ли это меш с одними параметрами и отсечением граней — можно ли нарисовать их одним инстансным вызовом
bool sameMesh( const MeshBatch& a, const MeshBatch& b )
{
	return a.material == b.material && a.params == b.params && a.indexOffset == b.indexOffset &&
		   a.vertexOffset == b.vertexOffset && a.indexCount == b.indexCount && a.state.blendMode == b.state.blendMode &&
		   a.state.twoSided == b.state.twoSided && a.mirrored == b.mirrored;
}

// Ключ прозрачной команды: дальние раньше, при равном расстоянии — порядок объектов сцены и номер
uint64_t transparentKey( float distance, uint32_t ownerOrder, uint32_t sequence )
{
	uint32_t bits;
	const float positive = std::max( distance, 0.0f );
	std::memcpy( &bits, &positive, sizeof( bits ) );	// у неотрицательных float порядок битов совпадает с порядком чисел
	return ( static_cast<uint64_t>( 0xFFFFFFFFu - bits ) << 32 ) | ( static_cast<uint64_t>( ownerOrder & 0xFF ) << 24 ) |
		   ( sequence & 0xFFFFFF );
}

}

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

	m_instanceBuffer.createBuffer( sizeof( InstanceTransform ), maxInstancesPerDraw );
	m_instanceTransforms.reserve( maxInstancesPerDraw );

	return m_samplerState.initialize();
}

void Renderer::render( Scene& scene, const FrameContext& frame, bool wireframe )
{
	m_gpuProfiler.beginFrame();
	m_meshCount = 0;
	m_meshDraws = 0;
	measure( "preparePipeline", [&] { preparePipeline( scene, frame ); } );

	measure( "Compute Pass", [&]
	{
		for( SceneObject* object : scene.objects() )
		{
			object->compute( frame );
		}
	} );

	// Меши с главного вида; позже — и с видов каскадов теней
	const auto collectStart = std::chrono::high_resolution_clock::now();
	collect( scene, frame.view );
	buildCommands();
	const auto collectEnd = std::chrono::high_resolution_clock::now();
	m_gui.addCounterInfo( "Collect meshes = %.3f ms",
						  std::chrono::duration_cast<std::chrono::microseconds>( collectEnd - collectStart ).count() / 1000.0f );

	DMD3D::instance().BeginScene( 0.004f, 0.004f, 0.004f, 1.0f );

	// Базовое состояние кадра; объекты меняют его только в своей области видимости
	const RasterState frameRaster = wireframe ? RasterState::wireframe : RasterState::solid;
	ScopedRenderState frameState( frameRaster );

	executePass( MeshPass::opaque, frame.view, frameRaster );

	{
		// Небо после непрозрачных: пиксели, закрытые сценой, отбрасывает ранняя проверка глубины
		ScopedRenderState skyState( DepthState::readOnlyLessEqual );
		executePass( MeshPass::sky, frame.view, frameRaster );
	}

	{
		// Полупрозрачные не пишут глубину: иначе закрыли бы то, что за ними рисуется позже
		ScopedRenderState transparentState( BlendState::alpha, DepthState::readOnly );
		executePass( MeshPass::transparent, frame.view, frameRaster );
	}

	measure( "Post process", [&] { m_postProcess.render(); } );
	m_gui.addCounterInfo( "Meshes = %.0f", static_cast<float>( m_meshCount ) );
	m_gui.addCounterInfo( "Mesh draw calls = %.0f", static_cast<float>( m_meshDraws ) );

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
	line += " meshes " + std::to_string( m_meshCount ) + " in " + std::to_string( m_meshDraws ) + " draws";
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
	// Константы кадра — главный вид: по нему считают и compute-проходы (кольцо расстановки вокруг камеры)
	pipeline().shaderConstant().beginFrame( lightCount );
	pipeline().shaderConstant().setViewBuffer( frame.view );
}

void Renderer::collect( Scene& scene, const RenderView& view )
{
	m_collector.clear();
	uint32_t order = 0;
	for( SceneObject* object : scene.objects() )
	{
		if( object->visible() )
		{
			m_collector.beginObject( object, order );
			object->collectMeshes( view, m_collector );
		}
		++order;
	}
}

void Renderer::buildCommands()
{
	for( auto& commands : m_commands )
		commands.clear();

	const std::vector<MeshBatch>& meshes = m_collector.meshes();
	for( uint32_t i = 0; i < meshes.size(); ++i )
	{
		const MeshBatch& batch = meshes[i];
		const MeshPass pass = passFor( batch.state.blendMode );
		uint64_t key;
		if( pass == MeshPass::transparent )
		{
			key = transparentKey( batch.distance, batch.ownerOrder, i );
		}
		else
		{
			const uint64_t phase = static_cast<uint64_t>( batch.material->phaseFor( *batch.params ) ) & 0xF;
			const uint64_t raster = static_cast<uint64_t>( materialRasterState( batch.state.twoSided, batch.mirrored,
																			   RasterState::solid ) ) & 0xF;
			// Группа (меш с параметрами) последней: одинаковые меши встают подряд — для инстансного вызова
			key = ( static_cast<uint64_t>( batch.ownerOrder & 0xFF ) << 56 ) |
				  ( static_cast<uint64_t>( batch.materialId & 0xFFFF ) << 40 ) | ( phase << 36 ) | ( raster << 32 ) |
				  batch.instanceGroup;
		}
		m_commands[static_cast<uint32_t>( pass )].push_back( { key, i, false } );
	}

	const std::vector<CustomBatch>& customs = m_collector.customs();
	for( uint32_t i = 0; i < customs.size(); ++i )
	{
		const CustomBatch& batch = customs[i];
		for( uint32_t pass = 0; pass < meshPassCount; ++pass )
		{
			if( !( batch.passMask & passBit( static_cast<MeshPass>( pass ) ) ) )
				continue;
			const uint64_t key = static_cast<MeshPass>( pass ) == MeshPass::transparent ?
								 transparentKey( batch.distance, batch.ownerOrder, static_cast<uint32_t>( meshes.size() ) + i ) :
								 static_cast<uint64_t>( batch.ownerOrder & 0xFF ) << 56;
			m_commands[pass].push_back( { key, i, true } );
		}
	}

	for( auto& commands : m_commands )
		std::stable_sort( commands.begin(), commands.end(),
						  []( const DrawCommand& a, const DrawCommand& b ) { return a.key < b.key; } );
}

void Renderer::executePass( MeshPass pass, const RenderView& view, RasterState frameRaster )
{
	// Проход не полагается на состояние, оставленное прошлым: своя цель, область вывода, без чужих ресурсов.
	// Глубину и блендинг задаёт ScopedRenderState кадра и прохода, растеризатор команд восстанавливается после прохода
	DMD3D::instance().setSceneTarget();
	DMD3D::instance().unbindTransientResources();
	ScopedRenderState passState;

	static const char* const passNames[] = { "Pass opaque", "Pass sky", "Pass transparent" };
	m_gpuProfiler.beginScope( passNames[static_cast<int>( pass )] );

	const RenderContext context{ view, pass, frameRaster, pipeline().shaderConstant(), m_vertexPool };
	const std::vector<MeshBatch>& meshes = m_collector.meshes();
	const std::vector<CustomBatch>& customs = m_collector.customs();
	const std::vector<DrawCommand>& commands = m_commands[static_cast<uint32_t>( pass )];
	auto ownerOf = [&]( const DrawCommand& command ) -> const SceneObject*
	{
		return command.custom ? customs[command.index].owner : meshes[command.index].owner;
	};

	for( size_t begin = 0; begin < commands.size(); )
	{
		// Серия команд одного объекта — одна строка времени CPU и GPU под его именем
		const SceneObject* owner = ownerOf( commands[begin] );
		size_t end = begin + 1;
		while( end < commands.size() && ownerOf( commands[end] ) == owner )
			++end;

		measure( owner->name(), [&]
		{
			bool poolBound = false;
			for( size_t i = begin; i < end; ++i )
			{
				// Свой вызов объекта может привязать свои буферы: после него общий буфер привязывается заново
				if( !poolBound )
				{
					m_vertexPool.setBuffers();
					poolBound = true;
				}
				if( commands[i].custom )
				{
					customs[commands[i].index].owner->renderCustom( context );
					poolBound = false;
				}
				else
				{
					// Подряд одинаковые непрозрачные меши (тот же меш, параметры, растеризатор) — одним вызовом
					const MeshBatch& batch = meshes[commands[i].index];
					size_t last = i;
					if( pass != MeshPass::transparent && batch.material->supportsInstancing() )
					{
						while( last + 1 < end && !commands[last + 1].custom &&
							   sameMesh( batch, meshes[commands[last + 1].index] ) )
							++last;
					}
					if( last > i )
						drawMeshInstanced( commands, i, last, context );
					else
						drawMesh( batch, context );
					i = last;
				}
			}
		} );
		begin = end;
	}

	m_gpuProfiler.endScope();
}

void Renderer::drawMeshInstanced( const std::vector<DrawCommand>& commands, size_t first, size_t last,
								  const RenderContext& context )
{
	const std::vector<MeshBatch>& meshes = m_collector.meshes();
	const MeshBatch& batch = meshes[commands[first].index];
	DMShader* shader = batch.material;
	DMD3D::instance().setState( materialRasterState( batch.state.twoSided, batch.mirrored, context.frameRaster ) );
	shader->setPass( shader->phaseFor( *batch.params, true ) );
	shader->setParams( *batch.params );

	for( size_t chunk = first; chunk <= last; chunk += maxInstancesPerDraw )
	{
		const size_t count = std::min<size_t>( last - chunk + 1, maxInstancesPerDraw );
		m_instanceTransforms.clear();
		for( size_t i = chunk; i < chunk + count; ++i )
		{
			// HLSL читает матрицы по столбцам — транспонирование, как у константного буфера объекта
			const XMMATRIX& world = meshes[commands[i].index].world;
			m_instanceTransforms.push_back( { XMMatrixTranspose( world ), XMMatrixTranspose( normalMatrix( world ) ) } );
		}
		m_instanceBuffer.updateData( m_instanceTransforms.data(), count * sizeof( InstanceTransform ) );
		m_instanceBuffer.setToSlot( SLOT_INSTANCE_DATA, SRVType::vs );
		shader->renderInstanced( batch.indexCount, batch.vertexOffset, batch.indexOffset, static_cast<int>( count ) );
		m_meshDraws++;
	}
	m_meshCount += static_cast<uint32_t>( last - first + 1 );
}

void Renderer::drawMesh( const MeshBatch& batch, const RenderContext& context )
{
	DMShader* shader = batch.material;
	DMD3D::instance().setState( materialRasterState( batch.state.twoSided, batch.mirrored, context.frameRaster ) );
	shader->setPass( shader->phaseFor( *batch.params ) );
	shader->setParams( *batch.params );
	shader->setDrawType( DMShader::by_index );
	context.constants.setPerObjectBuffer( batch.world );
	shader->render( batch.indexCount, batch.vertexOffset, batch.indexOffset );
	m_meshCount++;
	m_meshDraws++;
}

}
