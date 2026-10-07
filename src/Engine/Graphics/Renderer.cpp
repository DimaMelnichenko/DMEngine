#include "Renderer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include "Scene.h"
#include "System.h"
#include "D3D\ShaderCompiler.h"
#include "D3D\DMD3D.h"
#include "Materials\Material.h"
#include "Logger\Logger.h"

using namespace DirectX;

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

Renderer::Renderer( FrameStats& stats ) :
	m_stats( stats )
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
	m_stats.add( counterName + " = %.3f ms", std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0f );
}

bool Renderer::initialize( const PostProcess::Settings& postProcess, const std::optional<VolumetricFog::Settings>& fog,
						   uint32_t shadowResolution, bool depthPrepass )
{
	m_properties.setName( "Renderer" );
	m_properties.insert( "Depth prepass", depthPrepass );

	// Буфер сцены — размером с задний буфер
	DMD3D& d3d = DMD3D::instance();
	if( !m_sceneTargets.create( d3d.backBufferWidth(), d3d.backBufferHeight(), sceneClearColor ) )
		return false;

	if( !m_vertexPool.prepareMeshes() )
		return false;

	m_constants.initBuffers();

	if( !m_postProcess.initialize( postProcess ) )
		return false;

	if( !m_shadows.initialize( shadowResolution ) )
		return false;

	if( !m_fog.initialize( fog ) )
		return false;

	if( !m_gpuProfiler.initialize() )
		return false;

	m_instanceBuffer.createBuffer( sizeof( InstanceTransform ), maxInstancesPerDraw, "Instance transforms" );
	m_instanceTransforms.reserve( maxInstancesPerDraw );
	return true;
}

PassStates Renderer::passStates( const RenderState& shadowState ) const
{
	return { shadowState, SceneTargets::formats(), SceneTargets::depthOnlyFormats() };
}

void Renderer::warmPipelines( Scene& scene )
{
	// Состояние прохода теней — по настройкам солнца уровня (Shadow Slope Bias); нет солнца — по умолчанию
	m_warmedShadowState = ShadowCascades::renderState( scene.lights().sun() ? scene.lights().sun()->shadowSettings() : DMLight::ShadowSettings() );

	// Состояния, с которыми проходы рисуют меши материалов: цвет и depth prepass — растеризатор по двусторонности и
	// зеркальности (materialRasterState), в каркасном режиме кадра — wireframe; прозрачные — блендинг без записи глубины;
	// тени — состояние прохода теней; frontCulling — сфера неба. Все фазы каждого материала
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
	depthStates.push_back( m_warmedShadowState );
	// Проход opaqueDepthRead — у материалов, которые читают глубину сцены: проверка «ближе или равно» без записи
	std::vector<RenderState> depthReadStates;
	for( RasterState raster : { RasterState::solid, RasterState::noCulling, RasterState::solidMirrored, RasterState::noCullingMirrored,
								RasterState::wireframe } )
		depthReadStates.push_back( { raster, DepthState::readOnlyNearOrEqual, BlendState::opaque } );
	for( auto& material : System::materials() )
	{
		if( Material* shader = material.second.get() )
		{
			shader->warmPipelines( colorStates, SceneTargets::formats(), shader->colorPhases() );
			shader->warmPipelines( depthStates, SceneTargets::depthOnlyFormats(), shader->depthPhases() );
			if( shader->readsSceneDepth() )
				shader->warmPipelines( depthReadStates, SceneTargets::formats(), shader->colorPhases() );
		}
	}
	// Свои вызовы объектов (террейн): их пайплайны для состояний проходов рендерера
	const PassStates states = passStates( m_warmedShadowState );
	for( SceneObject* object : scene.objects() )
		object->warmPipelines( states );
	LOG( "Pipelines after warm-up: " + std::to_string( DMD3D::instance().pipelineCount() ) );
	ShaderCompiler::instance().logSummary();
}

bool Renderer::bake( Scene& scene )
{
	const BakeContext context{ m_constants, m_vertexPool };
	for( SceneObject* object : scene.objects() )
	{
		if( !object->bake( context ) )
			return false;
	}
	return true;
}

void Renderer::warmShadowPipelines( Scene& scene )
{
	m_warmedShadowState = m_shadows.renderState();
	for( auto& material : System::materials() )
	{
		if( Material* shader = material.second.get() )
			shader->warmPipelines( { m_warmedShadowState }, SceneTargets::depthOnlyFormats(), shader->depthPhases() );
	}
	const PassStates states = passStates( m_warmedShadowState );
	for( SceneObject* object : scene.objects() )
		object->warmPipelines( states );
}

bool Renderer::resize()
{
	DMD3D& d3d = DMD3D::instance();
	return m_sceneTargets.create( d3d.backBufferWidth(), d3d.backBufferHeight(), sceneClearColor ) && m_postProcess.resize() &&
		   m_fog.resize();
}

void Renderer::render( Scene& scene, const FrameContext& frame, bool wireframe )
{
	m_gpuProfiler.beginFrame();
	m_meshCount = 0;
	m_meshDraws = 0;
	m_shadowMeshCount = 0;
	m_shadowDraws = 0;
	measure( "preparePipeline", [&] { preparePipeline( scene, frame ); } );

	// Виды каскадов теней — до compute: расстановка раскладывает списки инстансов на каждый вид кадра
	XMFLOAT3 shadowLightDirection;
	DMLight::ShadowSettings shadowSettings;
	scene.lights().shadowLight( shadowLightDirection, shadowSettings );
	m_shadowsActive = m_shadows.update( frame.view, shadowSettings, frame.toShadowLight, scene.bounds() );
	// Смещение теней сменилось (слайдер в GUI, тени перешли на луну) — пайплайны теней заново до прохода
	if( m_shadows.renderState() != m_warmedShadowState )
		warmShadowPipelines( scene );
	FrameContext computeFrame = frame;
	computeFrame.views[0] = &frame.view;
	computeFrame.viewCount = 1;
	if( m_shadowsActive )
	{
		for( uint32_t cascade = 0; cascade < ShadowCascades::cascadeCount; ++cascade )
			computeFrame.views[1 + cascade] = &m_shadows.cascadeView( cascade );
		computeFrame.viewCount = 1 + ShadowCascades::cascadeCount;
	}

	// Compute — при главном виде в константах кадра: кольцо расстановки считается вокруг камеры
	measure( "Compute Pass", [&]
	{
		for( SceneObject* object : scene.objects() )
		{
			object->compute( computeFrame );
		}
	} );
	// Пересчёт освещения окружением мог закончиться в compute неба: масштаб — по показанному результату
	m_constants.setSkyLightScale( scene.skyLightScale() );
	m_constants.setViewBuffer( frame.view );

	const bool depthPrepass = m_properties["Depth prepass"].data<bool>();
	const auto collectStart = std::chrono::high_resolution_clock::now();
	collect( scene, frame.view, m_collector );
	buildCommands( depthPrepass );
	const auto collectEnd = std::chrono::high_resolution_clock::now();
	m_stats.add( "Collect meshes = %.3f ms",
						  std::chrono::duration_cast<std::chrono::microseconds>( collectEnd - collectStart ).count() / 1000.0f );

	renderShadows( scene, frame );

	// Проходы сцены — в HDR-буфер: очистка цвета и глубины (0 — дальняя плоскость при обратной глубине)
	m_sceneTargets.clear();
	// Карта теней — пиксельным шейдерам проходов сцены (после рисования в неё и смены цели)
	m_shadows.bindForReceivers( scene.lights().shadowLight( shadowLightDirection, shadowSettings ) );

	// Объём тумана — по карте теней (лучи в дымке), до проходов сцены: его читает освещение каждого пикселя
	if( m_fog.active() )
		measure( "Volumetric fog", [&] { m_fog.render( frame.view, m_shadows.shaderView() ); } );

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

	if( depthPrepass && !m_commands[static_cast<uint32_t>( MeshPass::opaqueDepthRead )].empty() )
	{
		// Непрозрачные, которым нужна глубина сцены (импостеры со смещением глубины): глубина только для чтения и видна
		// шейдерам; закрытое ближе растеризованной геометрии отбрасывает ранняя проверка «ближе или равно»
		ScopedRenderState depthReadState( DepthState::readOnlyNearOrEqual );
		executePass( MeshPass::opaqueDepthRead, frame.view, frameRaster, true );
	}

	{
		// Небо после непрозрачных: пиксели, закрытые сценой, отбрасывает ранняя проверка глубины
		ScopedRenderState skyState( DepthState::readOnlyNearOrEqual );
		executePass( MeshPass::sky, frame.view, frameRaster );
	}

	// Цвет сцены за полупрозрачными (SceneColor в UE: преломление воды) — копия того, что нарисовано до них
	if( m_sceneColorRead )
	{
		m_gpuProfiler.beginScope( "Scene color copy" );
		m_sceneTargets.copyColor();
		m_gpuProfiler.endScope();
	}

	{
		// Полупрозрачные не пишут глубину: иначе закрыли бы то, что за ними рисуется позже
		ScopedRenderState transparentState( BlendState::alpha, DepthState::readOnly );
		executePass( MeshPass::transparent, frame.view, frameRaster );
	}

	measure( "Post process", [&] { m_postProcess.render( m_sceneTargets.colorView(), m_gpuProfiler, frame.elapsedTime / 1000.0f ); } );
	m_stats.add( "Exposure EV100 = %.2f", m_postProcess.ev100() );
	m_sunGroundIlluminance = scene.lights().sunGroundIlluminance();
	m_stats.add( "Sun illuminance at ground = %.0f lx", m_sunGroundIlluminance );
	if( const SunPosition* sunPosition = scene.lights().sunPosition() )
	{
		const SunPosition::Angles angles = sunPosition->angles();
		m_stats.add( "Sun elevation = %.2f deg", angles.elevation );
		m_stats.add( "Sun azimuth = %.2f deg", angles.azimuth );
		const SunPosition::Moon moon = sunPosition->moon();
		m_stats.add( "Moon elevation = %.2f deg", moon.angles.elevation );
		m_stats.add( "Moon azimuth = %.2f deg", moon.angles.azimuth );
		m_stats.add( "Moon illuminated = %.2f", moon.illuminatedFraction );
	}
	m_stats.add( "Meshes = %.0f", static_cast<float>( m_meshCount ) );
	m_stats.add( "Mesh draw calls = %.0f", static_cast<float>( m_meshDraws ) );
	m_stats.add( "Shadow meshes = %.0f", static_cast<float>( m_shadowMeshCount ) );
	m_stats.add( "Shadow draw calls = %.0f", static_cast<float>( m_shadowDraws ) );

	m_gpuProfiler.endFrame();
	reportGpuTimes();
}

void Renderer::reportGpuTimes()
{
	// Время GPU отстаёт от кадра на несколько кадров: запросы читаются без ожидания
	m_stats.add( "GPU frame = %.3f ms", m_gpuProfiler.frameMilliseconds() );
	for( const auto& [name, milliseconds] : m_gpuProfiler.results() )
		m_stats.add( "GPU " + name + " = %.3f ms", milliseconds );

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
	// установка источников света
	int lightCount = scene.lights().setBuffer( SLOT_LIGHTS );
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
	// Туман: свет в объёме — в долях яркости, которую экспозиция делает белой (EV100 с отставанием в несколько кадров)
	frameParameters.fog = m_fog.frameParameters( frame.view, 1.2f * std::exp2( m_postProcess.ev100() ) );
	m_constants.beginFrame( frameParameters );
	// Экспозиция прошлого кадра — шейдерам сцены (pre-exposure)
	m_postProcess.bindExposure();
	m_constants.setViewBuffer( frame.view );
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
	m_sceneColorRead = false;
	m_sceneDepthRead = false;

	const std::vector<MeshBatch>& meshes = m_collector.meshes();
	for( uint32_t i = 0; i < meshes.size(); ++i )
	{
		const MeshBatch& batch = meshes[i];
		const MeshPass pass = passFor( batch.state.blendMode, batch.material->readsSceneDepth(), depthPrepass );
		uint64_t key;
		if( pass == MeshPass::transparent )
		{
			key = transparentKey( batch.distance, batch.ownerOrder, i );
			m_sceneColorRead |= batch.material->readsSceneColor();
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
			const bool transparent = static_cast<MeshPass>( pass ) == MeshPass::transparent;
			const uint64_t key = transparent ?
								 transparentKey( batch.distance, batch.ownerOrder, static_cast<uint32_t>( meshes.size() ) + i ) :
								 static_cast<uint64_t>( batch.ownerOrder & 0xFF ) << 56;
			m_commands[pass].push_back( { key, i, true } );
			m_sceneColorRead |= transparent && batch.readsSceneColor;
			m_sceneDepthRead |= transparent && batch.readsSceneDepth;
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
	// Каскады посчитаны в render() перед compute объектов
	if( !m_shadowsActive )
		return;

	const auto start = std::chrono::high_resolution_clock::now();
	// Состояние прохода теней — растеризатор теней со смещением глубины солнца (часть пайплайнов, warmShadowPipelines)
	ScopedRenderState shadowState( m_shadows.renderState() );

	// Время — одной областью на каскад: имена объектов в строке «GPU average» остаются за проходами сцены
	m_gpuProfiler.beginScope( "Shadow depths" );
	for( uint32_t cascade = 0; cascade < ShadowCascades::cascadeCount; ++cascade )
	{
		const RenderView& view = m_shadows.cascadeView( cascade );
		collect( scene, view, m_shadowCollector );
		buildShadowCommands();

		m_constants.setViewBuffer( view );
		m_shadows.beginCascade( cascade );
		m_gpuProfiler.beginScope( "Shadow cascade " + std::to_string( cascade ) );
		const RenderContext context{ view, MeshPass::csmShadowDepth, RasterState::csmShadowDepth,
									 m_constants, m_vertexPool };
		executeCommands( m_shadowCollector, m_shadowCommands, context, false );
		m_gpuProfiler.endScope();
	}
	m_gpuProfiler.endScope();

	// Проходы сцены — снова с главного вида
	m_constants.setViewBuffer( frame.view );
	const auto end = std::chrono::high_resolution_clock::now();
	m_stats.add( "Shadow depths = %.3f ms",
						  std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0f );
}

void Renderer::executePass( MeshPass pass, const RenderView& view, RasterState frameRaster, bool depthFromPrepass )
{
	// Проход не полагается на состояние, оставленное прошлым: своя цель, область вывода, без чужих ресурсов.
	// Глубину и блендинг задаёт ScopedRenderState кадра и прохода, растеризатор команд восстанавливается после прохода.
	static const char* const passNames[] = { "Pass depth prepass", "Pass opaque", "Pass opaque depth read", "Pass sky", "Pass transparent" };
	static_assert( std::size( passNames ) == scenePassCount );

	// Объявление прохода: буфер сцены (depth prepass — без цели цвета), читает ресурсы сцены — карту теней и экспозицию
	// (освещение окружением и объём воздушной перспективы привязывают объекты неба)
	DMD3D& d3d = DMD3D::instance();
	PassDesc desc;
	desc.name = passNames[static_cast<int>( pass )];
	// opaqueDepthRead — с глубиной только для чтения: она же — глубина сцены для шейдеров (SLOT_SCENE_DEPTH); так же
	// transparent, если в нём читают цвет сцены (копия — SLOT_SCENE_COLOR) или только глубину (частицы)
	const bool sceneColorRead = pass == MeshPass::transparent && m_sceneColorRead;
	const bool depthRead = pass == MeshPass::opaqueDepthRead || sceneColorRead || ( pass == MeshPass::transparent && m_sceneDepthRead );
	if( pass != MeshPass::depthPrepass )
		desc.colors = { { &m_sceneTargets.colorTarget(), "scene color" } };
	desc.depth = depthRead ? PassDesc::Target{ &m_sceneTargets.depthReadTarget(), "scene depth" } :
							 PassDesc::Target{ &m_sceneTargets.depthTarget(), "scene depth" };
	desc.width = m_sceneTargets.width();
	desc.height = m_sceneTargets.height();
	desc.reads = { { &m_shadows.shaderView(), "shadow map" }, { &m_postProcess.exposureView(), "exposure" } };
	if( depthRead )
		desc.reads.push_back( { &m_sceneTargets.depthView(), "scene depth" } );
	if( sceneColorRead )
		desc.reads.push_back( { &m_sceneTargets.colorCopyView(), "scene color copy" } );
	d3d.beginPass( desc );
	if( depthRead )
		d3d.setSRV( SLOT_SCENE_DEPTH, m_sceneTargets.depthView() );
	if( sceneColorRead )
		d3d.setSRV( SLOT_SCENE_COLOR, m_sceneTargets.colorCopyView() );
	ScopedRenderState passState;
	m_gpuProfiler.beginScope( passNames[static_cast<int>( pass )] );
	const RenderContext context{ view, pass, frameRaster, m_constants, m_vertexPool, depthFromPrepass };
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
	Material* shader = batch.material;
	DMD3D& d3d = DMD3D::instance();
	d3d.setState( materialRasterState( batch.state.twoSided, batch.mirrored, context.frameRaster ) );
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
		m_instanceBuffer.setToSlot( SLOT_INSTANCE_DATA );
		d3d.drawIndexedInstanced( batch.indexCount, static_cast<uint32_t>( count ), batch.indexOffset, batch.vertexOffset );
		countMeshes( context.pass, 0, 1 );
	}
	countMeshes( context.pass, static_cast<uint32_t>( last - first + 1 ), 0 );
}

void Renderer::drawMesh( const MeshBatch& batch, const RenderContext& context )
{
	Material* shader = batch.material;
	DMD3D& d3d = DMD3D::instance();
	d3d.setState( materialRasterState( batch.state.twoSided, batch.mirrored, context.frameRaster ) );
	const ShaderPhaseOptions options = meshPhaseOptions( batch, false, context.depthFromPrepass && inDepthPrepass( batch ) );
	shader->setPass( isDepthOnlyPass( context.pass ) ? shader->depthPhaseFor( *batch.params, options ) :
					 shader->phaseFor( *batch.params, options ) );
	shader->setParams( *batch.params );
	context.constants.setPerObjectBuffer( batch.world, batch.lodDither );
	d3d.drawIndexed( batch.indexCount, batch.indexOffset, batch.vertexOffset );
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
