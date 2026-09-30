#include "Renderer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include "Scene.h"
#include "System.h"
#include "D3D\ShaderCompiler.h"
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
		   a.state.twoSided == b.state.twoSided && a.mirrored == b.mirrored && a.lodDither == 0.0f && b.lodDither == 0.0f;
}

// Вариант шейдера меша: инстансный вызов, глубина из depth prepass и смена LOD дизерингом (экземпляр в полосе перехода)
ShaderPhaseOptions meshPhaseOptions( const MeshBatch& batch, bool instanced, bool depthFromPrepass )
{
	ShaderPhaseOptions options;
	options.instanced = instanced;
	options.depthFromPrepass = depthFromPrepass;
	options.lodDither = batch.lodDither != 0.0f;
	return options;
}

// Рисуется ли меш в depth prepass: непрозрачный или Masked с вариантом материала «только глубина». Остальные
// непрозрачные (материалы без варианта глубины) в проходе цвета пишут глубину сами
bool inDepthPrepass( const MeshBatch& batch )
{
	return batch.state.blendMode != BlendMode::translucent && batch.material->depthPhaseFor( *batch.params ) >= 0;
}

// Ключ непрозрачной команды: объекты в порядке сцены, внутри — материал, вариант шейдера, растеризатор; группа (меш
// с параметрами) последней — одинаковые меши встают подряд для инстансного вызова
uint64_t opaqueKey( const MeshBatch& batch, int phase )
{
	const uint64_t raster = static_cast<uint64_t>( materialRasterState( batch.state.twoSided, batch.mirrored,
																	   RasterState::solid ) ) & 0xF;
	return ( static_cast<uint64_t>( batch.ownerOrder & 0xFF ) << 56 ) |
		   ( static_cast<uint64_t>( batch.materialId & 0xFFFF ) << 40 ) |
		   ( ( static_cast<uint64_t>( phase ) & 0xF ) << 36 ) | ( raster << 32 ) | batch.instanceGroup;
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

bool Renderer::initialize( const PostProcess::Settings& postProcess, uint32_t shadowResolution, bool depthPrepass )
{
	m_properties.setName( "Renderer" );
	m_properties.insert( "Depth prepass", depthPrepass );

	if( !m_vertexPool.prepareMeshes() )
		return false;

	if( !m_postProcess.initialize( postProcess ) )
		return false;

	if( !m_shadows.initialize( shadowResolution ) )
		return false;

	if( !m_gpuProfiler.initialize() )
		return false;

	m_instanceBuffer.createBuffer( sizeof( InstanceTransform ), maxInstancesPerDraw );
	m_instanceTransforms.reserve( maxInstancesPerDraw );

	warmPipelines();
	return m_samplerState.initialize();
}

void Renderer::warmPipelines()
{
	// Состояния, с которыми проходы рисуют меши материалов: цвет и depth prepass — растеризатор по двусторонности и
	// зеркальности (materialRasterState), в каркасном режиме кадра — wireframe; прозрачные — блендинг без записи глубины;
	// тени — csmShadowDepth; frontCulling — сфера неба. Все фазы каждого материала
	// Цели: цвет — буфер сцены (HDR + глубина); depth prepass и каскады теней — только глубина (форматы одинаковы —
	// один пайплайн на оба)
	std::vector<RenderState> colorStates;
	std::vector<RenderState> depthStates;
	for( RasterState raster : { RasterState::solid, RasterState::noCulling, RasterState::solidMirrored, RasterState::noCullingMirrored,
								RasterState::wireframe, RasterState::frontCulling } )
	{
		colorStates.push_back( { raster, DepthState::enabled, BlendState::opaque } );
		colorStates.push_back( { raster, DepthState::readOnlyEqual, BlendState::opaque } );
		colorStates.push_back( { raster, DepthState::readOnly, BlendState::alpha } );
		depthStates.push_back( { raster, DepthState::enabled, BlendState::opaque } );
	}
	depthStates.push_back( { RasterState::csmShadowDepth, DepthState::enabled, BlendState::opaque } );
	for( auto& material : System::materials() )
	{
		if( material.second && material.second->m_shader )
		{
			DMShader& shader = *material.second->m_shader;
			shader.warmPipelines( colorStates, DMD3D::sceneFormats(), shader.colorPhases() );
			shader.warmPipelines( depthStates, DMD3D::depthOnlyFormats(), shader.depthPhases() );
		}
	}
	LOG( "Pipelines after warm-up: " + std::to_string( DMD3D::instance().pipelineCount() ) );
	ShaderCompiler::instance().logSummary();
}

bool Renderer::resize()
{
	return m_postProcess.resize();
}

void Renderer::render( Scene& scene, const FrameContext& frame, bool wireframe )
{
	m_gpuProfiler.beginFrame();
	m_meshCount = 0;
	m_meshDraws = 0;
	m_shadowMeshCount = 0;
	m_shadowDraws = 0;
	measure( "preparePipeline", [&] { preparePipeline( scene, frame ); } );

	// Compute — при главном виде в константах кадра: кольцо расстановки считается вокруг камеры
	measure( "Compute Pass", [&]
	{
		for( SceneObject* object : scene.objects() )
		{
			object->compute( frame );
		}
	} );
	// Пересчёт освещения окружением мог закончиться в compute неба: масштаб — по показанному результату
	pipeline().shaderConstant().setSkyLightScale( scene.skyLightScale() );
	pipeline().shaderConstant().setViewBuffer( frame.view );

	const bool depthPrepass = m_properties["Depth prepass"].data<bool>();
	const auto collectStart = std::chrono::high_resolution_clock::now();
	collect( scene, frame.view, m_collector );
	buildCommands( depthPrepass );
	const auto collectEnd = std::chrono::high_resolution_clock::now();
	m_gui.addCounterInfo( "Collect meshes = %.3f ms",
						  std::chrono::duration_cast<std::chrono::microseconds>( collectEnd - collectStart ).count() / 1000.0f );

	renderShadows( scene, frame );

	DMD3D::instance().BeginScene( 0.004f, 0.004f, 0.004f, 1.0f );
	// Карта теней — пиксельным шейдерам проходов сцены (после рисования в неё и смены цели)
	XMFLOAT3 toShadowLight;
	DMLight::ShadowSettings shadowSettings;
	m_shadows.bindForReceivers( scene.lights().shadowLight( toShadowLight, shadowSettings ) );

	// Базовое состояние кадра; объекты меняют его только в своей области видимости
	const RasterState frameRaster = wireframe ? RasterState::wireframe : RasterState::solid;
	ScopedRenderState frameState( frameRaster );

	if( depthPrepass )
	{
		// Сначала только глубина непрозрачных: проход цвета освещает каждый пиксель один раз — ближайшую поверхность
		ScopedRenderState prepassState( DepthState::enabled );
		executePass( MeshPass::depthPrepass, frame.view, frameRaster );
	}

	{
		// После prepass — проверка на равенство без записи (как CF_Equal базового прохода UE при полном prepass)
		ScopedRenderState opaqueState( depthPrepass ? DepthState::readOnlyEqual : DepthState::enabled );
		executePass( MeshPass::opaque, frame.view, frameRaster, depthPrepass );
	}

	{
		// Небо после непрозрачных: пиксели, закрытые сценой, отбрасывает ранняя проверка глубины
		ScopedRenderState skyState( DepthState::readOnlyNearOrEqual );
		executePass( MeshPass::sky, frame.view, frameRaster );
	}

	{
		// Полупрозрачные не пишут глубину: иначе закрыли бы то, что за ними рисуется позже
		ScopedRenderState transparentState( BlendState::alpha, DepthState::readOnly );
		executePass( MeshPass::transparent, frame.view, frameRaster );
	}

	measure( "Post process", [&] { m_postProcess.render( m_gpuProfiler, frame.elapsedTime / 1000.0f ); } );
	m_gui.addCounterInfo( "Exposure EV100 = %.2f", m_postProcess.ev100() );
	m_sunGroundIlluminance = scene.lights().sunGroundIlluminance();
	m_gui.addCounterInfo( "Sun illuminance at ground = %.0f lx", m_sunGroundIlluminance );
	if( const SunPosition* sunPosition = scene.lights().sunPosition() )
	{
		const SunPosition::Angles angles = sunPosition->angles();
		m_gui.addCounterInfo( "Sun elevation = %.2f deg", angles.elevation );
		m_gui.addCounterInfo( "Sun azimuth = %.2f deg", angles.azimuth );
		const SunPosition::Moon moon = sunPosition->moon();
		m_gui.addCounterInfo( "Moon elevation = %.2f deg", moon.angles.elevation );
		m_gui.addCounterInfo( "Moon azimuth = %.2f deg", moon.angles.azimuth );
		m_gui.addCounterInfo( "Moon illuminated = %.2f", moon.illuminatedFraction );
	}
	m_gui.addCounterInfo( "Meshes = %.0f", static_cast<float>( m_meshCount ) );
	m_gui.addCounterInfo( "Mesh draw calls = %.0f", static_cast<float>( m_meshDraws ) );
	m_gui.addCounterInfo( "Shadow meshes = %.0f", static_cast<float>( m_shadowMeshCount ) );
	m_gui.addCounterInfo( "Shadow draw calls = %.0f", static_cast<float>( m_shadowDraws ) );

	m_gpuProfiler.endFrame();
	reportGpuTimes();
}

void Renderer::reportGpuTimes()
{
	// Время GPU отстаёт от кадра на несколько кадров: запросы читаются без ожидания
	m_gui.addCounterInfo( "GPU frame = %.3f ms", m_gpuProfiler.frameMilliseconds() );
	for( const auto& [name, milliseconds] : m_gpuProfiler.results() )
		m_gui.addCounterInfo( "GPU " + name + " = %.3f ms", milliseconds );

	// Прогрев — первые кадры (пересчёт окружения неба, заполнение очередей) в среднее для лога не входят
	constexpr uint32_t warmupFrames = 60;
	if( ++m_frameIndex == warmupFrames )
		measureGpu( 3.0f, [this]( const std::string& line ) { logGpuAverage( line ); } );
	accumulateGpuMeasures();
}

void Renderer::logGpuAverage( const std::string& line )
{
	LOG( line );
}

void Renderer::measureGpu( float seconds, std::function<void( const std::string& )> done )
{
	GpuMeasure measure;
	measure.start = std::chrono::steady_clock::now();
	measure.duration = std::chrono::duration<float>( seconds );
	measure.done = std::move( done );
	measure.averages.push_back( { "frame" } );
	m_gpuMeasures.push_back( std::move( measure ) );
}

void Renderer::accumulateGpuMeasures()
{
	if( m_gpuMeasures.empty() || m_gpuProfiler.results().empty() )
		return;

	// Одноимённые области кадра (объект в нескольких проходах) сначала складываются, потом усредняются по кадрам
	std::vector<std::pair<std::string, float>> frameTimes;
	for( const auto& [name, milliseconds] : m_gpuProfiler.results() )
	{
		auto it = std::find_if( frameTimes.begin(), frameTimes.end(), [&]( const auto& entry ) { return entry.first == name; } );
		if( it == frameTimes.end() )
			frameTimes.push_back( { name, milliseconds } );
		else
			it->second += milliseconds;
	}

	const auto now = std::chrono::steady_clock::now();
	for( auto measure = m_gpuMeasures.begin(); measure != m_gpuMeasures.end(); )
	{
		std::vector<GpuAverage>& averages = measure->averages;
		averages[0].sum += m_gpuProfiler.frameMilliseconds();
		averages[0].count++;
		for( const auto& [name, milliseconds] : frameTimes )
		{
			auto it = std::find_if( averages.begin(), averages.end(), [&]( const GpuAverage& a ) { return a.name == name; } );
			if( it == averages.end() )
			{
				averages.push_back( { name } );
				it = averages.end() - 1;
			}
			it->sum += milliseconds;
			it->count++;
		}

		if( now - measure->start < measure->duration )
		{
			++measure;
			continue;
		}
		measure->done( gpuMeasureLine( *measure ) );
		measure = m_gpuMeasures.erase( measure );
	}
}

std::string Renderer::gpuMeasureLine( const GpuMeasure& measure ) const
{
	char value[32];
	std::string line = "GPU average over " + std::to_string( measure.averages[0].count ) + " frames, ms:";
	for( const GpuAverage& average : measure.averages )
	{
		std::snprintf( value, sizeof( value ), "%.3f", average.sum / std::max( average.count, 1u ) );
		line += " " + average.name + " " + value + ";";
	}
	line += " meshes " + std::to_string( m_meshCount ) + " in " + std::to_string( m_meshDraws ) + " draws; shadow meshes " +
			std::to_string( m_shadowMeshCount ) + " in " + std::to_string( m_shadowDraws ) + " draws";
	std::snprintf( value, sizeof( value ), "%.2f", m_postProcess.ev100() );
	line += std::string( "; EV100 " ) + value;
	std::snprintf( value, sizeof( value ), "%.0f", m_sunGroundIlluminance );
	line += std::string( "; sun " ) + value + " lx";
	return line;
}

PropertyContainer* Renderer::postProcessProperties()
{
	return m_postProcess.properties();
}

PropertyContainer* Renderer::shadowProperties()
{
	return m_shadows.properties();
}

PropertyContainer* Renderer::properties()
{
	return &m_properties;
}

PostProcess::Settings Renderer::postProcessSettings()
{
	return m_postProcess.settings();
}

void Renderer::preparePipeline( Scene& scene, const FrameContext& frame )
{
	m_samplerState.setDefaultSamplers();
	// установка источников света
	int lightCount = scene.lights().setBuffer( SLOT_LIGHTS, SRVType::ps );
	// Константы кадра — главный вид: по нему считают и compute-проходы (кольцо расстановки вокруг камеры)
	// Масштаб неба и освещения окружением; воздушная перспектива — только у атмосферы
	FrameParameters frameParameters;
	frameParameters.lightsCount = lightCount;
	frameParameters.skyLightScale = scene.skyLightScale();
	frameParameters.skyScale = scene.skyScale();
	frameParameters.aerialPerspectiveDistance = scene.hasAtmosphere() ? frame.view.farPlane : 0.0f;
	frameParameters.aerialPerspectiveScale = scene.aerialPerspectiveScale();
	frameParameters.gameTime = frame.gameTime;
	frameParameters.deltaTime = frame.elapsedTime / 1000.0f;
	frameParameters.wind = scene.wind().parameters();
	pipeline().shaderConstant().beginFrame( frameParameters );
	// Экспозиция прошлого кадра — шейдерам сцены (pre-exposure)
	m_postProcess.bindExposure();
	pipeline().shaderConstant().setViewBuffer( frame.view );
}

void Renderer::collect( Scene& scene, const RenderView& view, MeshCollector& collector )
{
	collector.clear();
	uint32_t order = 0;
	for( SceneObject* object : scene.objects() )
	{
		if( object->visible() )
		{
			collector.beginObject( object, order );
			object->collectMeshes( view, collector );
		}
		++order;
	}
}

void Renderer::buildCommands( bool depthPrepass )
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
			const bool prepass = depthPrepass && inDepthPrepass( batch );
			key = opaqueKey( batch, batch.material->phaseFor( *batch.params, meshPhaseOptions( batch, false, prepass ) ) );
			if( prepass )
			{
				const uint32_t prepassIndex = static_cast<uint32_t>( MeshPass::depthPrepass );
				const int depthPhase = batch.material->depthPhaseFor( *batch.params, meshPhaseOptions( batch, false, false ) );
				m_commands[prepassIndex].push_back( { opaqueKey( batch, depthPhase ), i, false } );
			}
		}
		m_commands[static_cast<uint32_t>( pass )].push_back( { key, i, false } );
	}

	const std::vector<CustomBatch>& customs = m_collector.customs();
	for( uint32_t i = 0; i < customs.size(); ++i )
	{
		const CustomBatch& batch = customs[i];
		for( uint32_t pass = 0; pass < scenePassCount; ++pass )
		{
			if( !( batch.passMask & passBit( static_cast<MeshPass>( pass ) ) ) ||
				( static_cast<MeshPass>( pass ) == MeshPass::depthPrepass && !depthPrepass ) )
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

void Renderer::buildShadowCommands()
{
	m_shadowCommands.clear();

	const std::vector<MeshBatch>& meshes = m_shadowCollector.meshes();
	for( uint32_t i = 0; i < meshes.size(); ++i )
	{
		const MeshBatch& batch = meshes[i];
		if( !batch.castsShadow || batch.state.blendMode == BlendMode::translucent )
			continue;
		const int phase = batch.material->depthPhaseFor( *batch.params, meshPhaseOptions( batch, false, false ) );
		if( phase < 0 )
			continue;
		const uint64_t key = ( static_cast<uint64_t>( batch.ownerOrder & 0xFF ) << 56 ) |
							 ( static_cast<uint64_t>( batch.materialId & 0xFFFF ) << 40 ) |
							 ( static_cast<uint64_t>( phase & 0xF ) << 36 ) | batch.instanceGroup;
		m_shadowCommands.push_back( { key, i, false } );
	}

	const std::vector<CustomBatch>& customs = m_shadowCollector.customs();
	for( uint32_t i = 0; i < customs.size(); ++i )
	{
		if( customs[i].passMask & passBit( MeshPass::csmShadowDepth ) )
			m_shadowCommands.push_back( { static_cast<uint64_t>( customs[i].ownerOrder & 0xFF ) << 56, i, true } );
	}

	std::stable_sort( m_shadowCommands.begin(), m_shadowCommands.end(),
					  []( const DrawCommand& a, const DrawCommand& b ) { return a.key < b.key; } );
}

void Renderer::renderShadows( Scene& scene, const FrameContext& frame )
{
	XMFLOAT3 toShadowLight;
	DMLight::ShadowSettings shadowSettings;
	scene.lights().shadowLight( toShadowLight, shadowSettings );
	if( !m_shadows.update( frame.view, shadowSettings, frame.toShadowLight, scene.bounds() ) )
		return;

	const auto start = std::chrono::high_resolution_clock::now();
	// Карта сейчас привязана к пиксельным шейдерам с прошлого кадра: рисовать в неё можно, только отвязав
	ScopedRenderState shadowState( RasterState::csmShadowDepth, DepthState::enabled, BlendState::opaque );

	// Время — одной областью на каскад: имена объектов в строке «GPU average» остаются за проходами сцены
	m_gpuProfiler.beginScope( "Shadow depths" );
	for( uint32_t cascade = 0; cascade < ShadowCascades::cascadeCount; ++cascade )
	{
		const RenderView& view = m_shadows.cascadeView( cascade );
		collect( scene, view, m_shadowCollector );
		buildShadowCommands();

		pipeline().shaderConstant().setViewBuffer( view );
		m_shadows.beginCascade( cascade );
		m_gpuProfiler.beginScope( "Shadow cascade " + std::to_string( cascade ) );
		const RenderContext context{ view, MeshPass::csmShadowDepth, RasterState::csmShadowDepth,
									 pipeline().shaderConstant(), m_vertexPool };
		executeCommands( m_shadowCollector, m_shadowCommands, context, false );
		m_gpuProfiler.endScope();
	}
	m_gpuProfiler.endScope();

	// Проходы сцены — снова с главного вида
	pipeline().shaderConstant().setViewBuffer( frame.view );
	const auto end = std::chrono::high_resolution_clock::now();
	m_gui.addCounterInfo( "Shadow depths = %.3f ms",
						  std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0f );
}

void Renderer::executePass( MeshPass pass, const RenderView& view, RasterState frameRaster, bool depthFromPrepass )
{
	// Проход не полагается на состояние, оставленное прошлым: своя цель, область вывода, без чужих ресурсов.
	// Глубину и блендинг задаёт ScopedRenderState кадра и прохода, растеризатор команд восстанавливается после прохода.
	static const char* const passNames[] = { "Pass depth prepass", "Pass opaque", "Pass sky", "Pass transparent" };
	static_assert( std::size( passNames ) == scenePassCount );

	// Объявление прохода: буфер сцены (depth prepass — без цели цвета), читает ресурсы сцены — карту теней и экспозицию
	// (освещение окружением и объём воздушной перспективы привязывают объекты неба)
	DMD3D& d3d = DMD3D::instance();
	PassDesc desc;
	desc.name = passNames[static_cast<int>( pass )];
	if( pass != MeshPass::depthPrepass )
		desc.colors = { { &d3d.sceneTarget(), "scene color" } };
	desc.depth = { &d3d.sceneDepthTarget(), "scene depth" };
	desc.width = d3d.sceneWidth();
	desc.height = d3d.sceneHeight();
	desc.reads = { { &m_shadows.shaderView(), "shadow map" }, { &m_postProcess.exposureView(), "exposure" } };
	d3d.beginPass( desc );
	ScopedRenderState passState;
	m_gpuProfiler.beginScope( passNames[static_cast<int>( pass )] );
	const RenderContext context{ view, pass, frameRaster, pipeline().shaderConstant(), m_vertexPool, depthFromPrepass };
	// Depth prepass — одной областью, как тени: строки объектов в «Statistic» и «GPU average» — их проходы цвета
	executeCommands( m_collector, m_commands[static_cast<uint32_t>( pass )], context, pass != MeshPass::depthPrepass );
	m_gpuProfiler.endScope();
}

void Renderer::executeCommands( const MeshCollector& collector, const std::vector<DrawCommand>& commands,
								const RenderContext& context, bool measureOwners )
{
	const std::vector<MeshBatch>& meshes = collector.meshes();
	const std::vector<CustomBatch>& customs = collector.customs();
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

		auto run = [&]
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
				// Меш или свой вызов, которого не было в depth prepass, в проходе цвета пишет глубину сам
				const bool prepassed = commands[i].custom ?
									   ( customs[commands[i].index].passMask & passBit( MeshPass::depthPrepass ) ) != 0 :
									   inDepthPrepass( meshes[commands[i].index] );
				std::optional<ScopedRenderState> ownDepth;
				if( context.depthFromPrepass && !prepassed )
					ownDepth.emplace( DepthState::enabled );
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
					if( context.pass != MeshPass::transparent && batch.material->supportsInstancing() )
					{
						while( last + 1 < end && !commands[last + 1].custom &&
							   sameMesh( batch, meshes[commands[last + 1].index] ) )
							++last;
					}
					if( last > i )
						drawMeshInstanced( meshes, commands, i, last, context );
					else
						drawMesh( batch, context );
					i = last;
				}
			}
		};
		if( measureOwners )
			measure( owner->name(), run );
		else
			run();
		begin = end;
	}
}

void Renderer::drawMeshInstanced( const std::vector<MeshBatch>& meshes, const std::vector<DrawCommand>& commands,
								  size_t first, size_t last, const RenderContext& context )
{
	const MeshBatch& batch = meshes[commands[first].index];
	DMShader* shader = batch.material;
	DMD3D::instance().setState( materialRasterState( batch.state.twoSided, batch.mirrored, context.frameRaster ) );
	const ShaderPhaseOptions options = meshPhaseOptions( batch, true, context.depthFromPrepass && inDepthPrepass( batch ) );
	shader->setPass( isDepthOnlyPass( context.pass ) ? shader->depthPhaseFor( *batch.params, options ) :
					 shader->phaseFor( *batch.params, options ) );
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
		countMeshes( context.pass, 0, 1 );
	}
	countMeshes( context.pass, static_cast<uint32_t>( last - first + 1 ), 0 );
}

void Renderer::drawMesh( const MeshBatch& batch, const RenderContext& context )
{
	DMShader* shader = batch.material;
	DMD3D::instance().setState( materialRasterState( batch.state.twoSided, batch.mirrored, context.frameRaster ) );
	const ShaderPhaseOptions options = meshPhaseOptions( batch, false, context.depthFromPrepass && inDepthPrepass( batch ) );
	shader->setPass( isDepthOnlyPass( context.pass ) ? shader->depthPhaseFor( *batch.params, options ) :
					 shader->phaseFor( *batch.params, options ) );
	shader->setParams( *batch.params );
	shader->setDrawType( DMShader::by_index );
	context.constants.setPerObjectBuffer( batch.world, batch.lodDither );
	shader->render( batch.indexCount, batch.vertexOffset, batch.indexOffset );
	countMeshes( context.pass, 1, 1 );
}

void Renderer::countMeshes( MeshPass pass, uint32_t meshes, uint32_t draws )
{
	if( pass == MeshPass::csmShadowDepth )
	{
		m_shadowMeshCount += meshes;
		m_shadowDraws += draws;
	}
	else if( pass != MeshPass::depthPrepass )
	{
		m_meshCount += meshes;
		m_meshDraws += draws;
	}
}

}
