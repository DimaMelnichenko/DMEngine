#include "Renderer.h"
#include "DrawSortKey.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include "Scene.h"
#include "Assets.h"
#include "D3D\ShaderCompiler.h"
#include "D3D\DMD3D.h"
#include "Materials\Material.h"
#include "Model\SkinningPalette.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Один ли это меш с одними параметрами и отсечением граней — можно ли нарисовать их одним инстансным вызовом (у модели
// со скелетом — с одним буфером матриц скиннинга: поза у каждого своя, первая кость — в данных экземпляра)
bool sameMesh( const MeshBatch& a, const MeshBatch& b )
{
	return a.material == b.material && a.params == b.params && a.indexOffset == b.indexOffset &&
		   a.vertexOffset == b.vertexOffset && a.indexCount == b.indexCount && a.state.blendMode == b.state.blendMode &&
		   a.state.twoSided == b.state.twoSided && a.mirrored == b.mirrored && a.lodDither == 0.0f && b.lodDither == 0.0f &&
		   a.skin == b.skin;
}

// Вариант шейдера меша: инстансный вызов, глубина из depth prepass, смена LOD дизерингом (экземпляр в полосе перехода) и
// скиннинг (модель со скелетом)
ShaderPhaseOptions meshPhaseOptions( const MeshBatch& batch, bool instanced, bool depthFromPrepass )
{
	ShaderPhaseOptions options;
	options.instanced = instanced;
	options.depthFromPrepass = depthFromPrepass;
	options.lodDither = batch.lodDither != 0.0f;
	options.skinned = batch.skin != nullptr;
	return options;
}

// Рисуется ли меш в depth prepass: непрозрачный или Masked с вариантом материала «только глубина». Остальные
// непрозрачные (материалы без варианта глубины) в проходе цвета пишут глубину сами
bool inDepthPrepass( const MeshBatch& batch )
{
	return batch.state.blendMode != BlendMode::translucent && batch.material->depthPhaseFor( *batch.params ) >= 0;
}

// Ключ непрозрачной команды меша — по полям его секции (DrawSortKey.h)
uint64_t opaqueKey( const MeshBatch& batch, int phase )
{
	const uint32_t raster = static_cast<uint32_t>( materialRasterState( batch.state.twoSided, batch.mirrored, RasterState::solid ) );
	return opaqueSortKey( batch.ownerOrder, batch.materialId, phase, raster, batch.instanceGroup );
}

// Миллисекунды с момента start — для счётчиков времени CPU кадра (FrameStats)
float millisecondsSince( std::chrono::high_resolution_clock::time_point start )
{
	return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - start ).count() / 1000.0f;
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

	if( !m_ambientOcclusion.initialize() )
		return false;

	if( !m_reflections.initialize() )
		return false;

	if( !m_gpuProfiler.initialize() )
		return false;

	m_instanceBuffer.createBuffer( sizeof( InstanceTransform ), maxInstancesPerDraw, "Instance transforms" );
	// Экспозиция съёмки сцены — одна запись ExposureState (Shaders/exposure.sh)
	m_captureExposure.createBuffer( 4 * sizeof( float ), 1, "Scene capture exposure" );
	m_instanceTransforms.reserve( maxInstancesPerDraw );
	m_features = { &m_shadows, &m_fog, &m_ambientOcclusion, &m_reflections, &m_postProcess };
	return true;
}

PassStates Renderer::passStates( const RenderState& shadowState ) const
{
	return { shadowState, SceneTargets::formats(), SceneTargets::depthOnlyFormats(), ScreenSpaceReflections::surfaceFormats() };
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
	for( auto& material : Assets::materials() )
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
	for( auto& material : Assets::materials() )
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
	if( !m_sceneTargets.create( d3d.backBufferWidth(), d3d.backBufferHeight(), sceneClearColor ) )
		return false;
	for( RenderFeature* feature : m_features )
	{
		if( !feature->resize() )
			return false;
	}
	return true;
}

void Renderer::render( Scene& scene, const FrameContext& frame, bool wireframe )
{
	m_gpuProfiler.beginFrame();
	m_meshCount = 0;
	m_meshDraws = 0;
	m_shadowMeshCount = 0;
	m_shadowDraws = 0;
	const bool depthPrepass = m_properties["Depth prepass"].data<bool>();
	// Съёмка сцены с другой камеры — по одной за кадр: её вид — среди видов кадра для compute (расстановка раскладывает в
	// него лес), рисуется после проходов главного вида
	takeCapture( scene, frame );
	measure( "preparePipeline", [&] { preparePipeline( scene, frame, depthPrepass ); } );
	const FrameContext computeFrame = prepareShadows( scene, frame );
	computeObjects( scene, computeFrame );

	// Сбор мешей с главного вида и раскладка по проходам с сортировкой — отдельно: у первого растёт число объектов, у
	// второго — мешей
	auto start = std::chrono::high_resolution_clock::now();
	collect( scene, frame.view, m_collector );
	m_stats.add( "Collect main view = %.3f ms", millisecondsSince( start ) );
	start = std::chrono::high_resolution_clock::now();
	buildCommands( m_collector, m_commands, depthPrepass, m_sceneColorRead, m_sceneDepthRead );
	m_stats.add( "Build commands = %.3f ms", millisecondsSince( start ) );

	renderShadows( scene, frame );
	renderScene( scene, frame, wireframe, depthPrepass );
	if( m_capture )
	{
		measure( "Scene capture", [&] { renderCapture( scene, frame, wireframe ? RasterState::wireframe : RasterState::solid ); } );
		m_capture.reset();
	}
	measure( "Post process", [&] { m_postProcess.render( m_sceneTargets.colorView(), m_sceneTargets.depthView(), frame.view, m_gpuProfiler,
														 frame.elapsedTime / 1000.0f ); } );
	reportFrameStats( scene );

	m_gpuProfiler.endFrame();
	reportGpuTimes();
}

FrameContext Renderer::prepareShadows( Scene& scene, const FrameContext& frame )
{
	// Виды каскадов теней — до compute: расстановка раскладывает списки инстансов на каждый вид кадра
	XMFLOAT3 shadowLightDirection;
	DMLight::ShadowSettings shadowSettings;
	scene.lights().shadowLight( shadowLightDirection, shadowSettings );
	m_shadowsActive = m_shadows.update( frame.view, shadowSettings, frame.toShadowLight, scene.bounds() );
	// Тень рельефа дальше каскадов — до compute и проходов сцены (её читают туман и все приёмники тени)
	if( m_shadowsActive )
		measure( "Terrain sun shadow", [&] { m_shadows.updateTerrainShadow( scene.terrainHeightSource().terrainHeight(),
																			   scene.farTerrainHeight(), frame.toShadowLight ); } );
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
		// Дальний каскад — шестым видом: расстановка раскладывает в него лес, чья тень ложится дальше дистанции теней
		if( m_shadows.farActive() )
			computeFrame.views[computeFrame.viewCount++] = &m_shadows.farView();
	}
	// Съёмка сцены — следующим видом
	if( m_capture )
	{
		m_capture->view.index = computeFrame.viewCount;
		computeFrame.views[computeFrame.viewCount++] = &m_capture->view;
	}
	return computeFrame;
}

void Renderer::computeObjects( Scene& scene, const FrameContext& computeFrame )
{
	// Compute — при главном виде в константах кадра: кольцо расстановки считается вокруг камеры. У объекта — своя область
	// «Compute: <объект>»: время GPU и CPU в «Statistic» и строках «GPU average» / «CPU average», метка в захвате PIX и у
	// сторожа кадра (FrameWatchdog: на каком объекте стоит кадр)
	measure( "Compute Pass", [&]
	{
		for( SceneObject* object : scene.objects() )
			measure( "Compute: " + object->name(), [&] { object->compute( computeFrame ); } );
	} );
	// Пересчёт освещения окружением мог закончиться в compute неба: масштаб — по показанному результату
	m_constants.setSkyLightScale( scene.skyLightScale() );
	m_constants.setViewBuffer( computeFrame.view );
}

void Renderer::renderScene( Scene& scene, const FrameContext& frame, bool wireframe, bool depthPrepass )
{
	// Проходы сцены — в HDR-буфер: очистка цвета и глубины (0 — дальняя плоскость при обратной глубине)
	m_sceneTargets.clear();
	// Карта теней — пиксельным шейдерам проходов сцены (после рисования в неё и смены цели)
	XMFLOAT3 shadowLightDirection;
	DMLight::ShadowSettings shadowSettings;
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

	// Затенение окружением — по глубине prepass, до прохода цвета: его читают материалы (без prepass его нет)
	if( m_ambientOcclusion.active() )
		measure( "Ambient occlusion", [&] { m_ambientOcclusion.render( frame.view, m_sceneTargets.depthView() ); } );

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

	// Экранные отражения: пирамида глубины сцены; поверхности, которые их просят (вода), — глубина и нормаль без ряби; затем
	// лучи по пирамиде; результат читает проход transparent (SLOT_SCREEN_REFLECTION)
	if( !m_commands[static_cast<uint32_t>( MeshPass::reflectionPrepass )].empty() )
	{
		measure( "Scene depth pyramid", [&] { m_reflections.buildPyramid( m_sceneTargets.depthView() ); } );
		m_reflections.clearSurfaces();
		{
			ScopedRenderState surfaceState( DepthState::enabled, BlendState::opaque );
			executePass( MeshPass::reflectionPrepass, frame.view, frameRaster );
		}
		measure( "Screen reflections", [&] { m_reflections.trace( m_sceneTargets.depthView() ); } );
	}

	// Маска лучей солнца — по небу до полупрозрачных: частицы (пушинки) и вода глубину не пишут и на фоне неба дали бы свои
	// лучи (PostProcess::renderLightShaftMask; солнца нет в кадре и рядом — прохода нет)
	m_postProcess.renderLightShaftMask( m_sceneTargets.colorView(), m_sceneTargets.depthView(), frame.view, frame.toShadowLight,
										m_gpuProfiler );

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
}

void Renderer::renderCapture( Scene& scene, const FrameContext& frame, RasterState frameRaster )
{
	const SceneCapture& capture = *m_capture;
	const RenderView& view = capture.view;
	collect( scene, view, m_captureCollector );
	bool sceneColorRead = false;
	bool sceneDepthRead = false;
	buildCommands( m_captureCollector, m_captureCommands, false, sceneColorRead, sceneDepthRead );

	// Константы вида: без затенения окружением по экрану и объёма тумана — их считали под главный вид (туман — по формуле);
	// объекты готовят свои ресурсы под вид съёмки (небо — объём воздушной перспективы)
	FrameParameters parameters = m_constants.frameParameters();
	parameters.ambientOcclusion = XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	parameters.fog.volumeDistance = 0.0f;
	m_constants.setViewBuffer( view, parameters );
	for( SceneObject* object : scene.objects() )
		object->beginCaptureView( view );
	// Тени — только дальние: каскады подогнаны под главную камеру
	XMFLOAT3 shadowLightDirection;
	DMLight::ShadowSettings shadowSettings;
	const int shadowLight = scene.lights().shadowLight( shadowLightDirection, shadowSettings );
	m_shadows.bindForReceivers( shadowLight, false );
	// Своя экспозиция (ExposureState: экспозиция, она же у цвета сцены, EV100)
	const float exposureState[4] = { capture.exposure, capture.exposure, m_postProcess.ev100(), 0.0f };
	m_captureExposure.updateData( exposureState, sizeof( exposureState ) );
	m_captureExposure.setToSlot( SLOT_EXPOSURE );

	DMD3D& d3d = DMD3D::instance();
	m_gpuProfiler.beginScope( "Scene capture" );
	d3d.beginPass( PassDesc{ "Scene capture clear", { { capture.color, "capture color" } }, { capture.depth, "capture depth" },
							 capture.width, capture.height } );
	d3d.clearTarget( *capture.color, capture.clear() );
	d3d.clearDepth( *capture.depth, 0.0f );
	{
		ScopedRenderState frameState( frameRaster );
		{
			ScopedRenderState opaqueState( DepthState::enabled );
			executePass( MeshPass::opaque, view, frameRaster, false, &capture );
		}
		if( capture.sky )
		{
			ScopedRenderState skyState( DepthState::readOnlyNearOrEqual );
			executePass( MeshPass::sky, view, frameRaster, false, &capture );
		}
	}
	m_gpuProfiler.endScope();
	if( capture.done )
		capture.done( capture );

	// Дальше — снова главный вид: константы, тени, экспозиция (объём воздушной перспективы — уже съёмки: после проходов
	// сцены его не читают, следующий кадр посчитает свой)
	m_constants.setViewBuffer( frame.view );
	m_shadows.bindForReceivers( shadowLight );
	d3d.setSRV( SLOT_EXPOSURE, m_postProcess.exposureView() );
}

void Renderer::takeCapture( Scene& scene, const FrameContext& frame )
{
	if( !m_captureRequests.empty() )
	{
		m_capture = std::move( m_captureRequests.front() );
		m_captureRequests.pop_front();
	}
	else
	{
		for( SceneObject* object : scene.objects() )
		{
			SceneCapture capture;
			if( object->requestCapture( frame, capture ) )
			{
				m_capture = std::move( capture );
				break;
			}
		}
	}
	if( !m_capture )
		return;
	m_capture->view.kind = RenderView::Kind::capture;
	if( m_capture->exposure <= 0.0f )
		m_capture->exposure = exposure();
}

float Renderer::exposure() const
{
	return m_postProcess.exposure();
}

void Renderer::reportFrameStats( Scene& scene )
{
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
	m_gpuMeasures.start( seconds, std::move( done ) );
}

void Renderer::accumulateGpuMeasures()
{
	if( !m_gpuMeasures.active() || m_gpuProfiler.results().empty() )
		return;
	FrameMeasures::Values values{ { "frame", m_gpuProfiler.frameMilliseconds() } };
	for( const auto& [name, milliseconds] : m_gpuProfiler.results() )
		values.push_back( { name, milliseconds } );
	m_gpuMeasures.accumulate( values, [this] { return gpuMeasureTail(); } );
}

std::string Renderer::gpuMeasureTail() const
{
	char value[32];
	std::string line = " meshes " + std::to_string( m_meshCount ) + " in " + std::to_string( m_meshDraws ) + " draws; shadow meshes " +
					   std::to_string( m_shadowMeshCount ) + " in " + std::to_string( m_shadowDraws ) + " draws";
	std::snprintf( value, sizeof( value ), "%.2f", m_postProcess.ev100() );
	line += std::string( "; EV100 " ) + value;
	std::snprintf( value, sizeof( value ), "%.0f", m_sunGroundIlluminance );
	line += std::string( "; sun " ) + value + " lx";
	// Замер с закреплённой частотой (-stableclocks) с обычным не сравнивают: мс другие
	if( DMD3D::instance().stableClocks() )
		line += "; stable clocks";
	return line;
}

PropertyContainer* Renderer::postProcessProperties()
{
	return m_postProcess.properties();
}

PropertyContainer* Renderer::postProcessPart( const char* name )
{
	return m_postProcess.part( name );
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

void Renderer::applyEnvironment( const PostProcess::Settings& postProcess, const std::optional<VolumetricFog::Settings>& fog )
{
	m_postProcess.apply( postProcess );
	m_fog.apply( fog );
	m_postProcess.properties()->markSaved();
	m_fog.properties()->markSaved();
}

void Renderer::preparePipeline( Scene& scene, const FrameContext& frame, bool depthPrepass )
{
	// установка источников света
	int lightCount = scene.lights().setBuffer( SLOT_LIGHTS );
	// Константы кадра — главный вид: по нему считают и compute-проходы (кольцо расстановки вокруг камеры). Небо, ветер и
	// вклад объектов сцены (тень облаков, детальная вода, следы в траве, поле ветра) — от сцены, туман, затенение
	// окружением и экспозиция — от возможностей рендерера: их карты и поля посчитают проходы этого кадра, а константы и
	// привязки нужны уже сейчас
	FrameParameters frameParameters;
	frameParameters.lightsCount = lightCount;
	frameParameters.gameTime = frame.gameTime;
	frameParameters.deltaTime = frame.elapsedTime / 1000.0f;
	scene.frameConstants( frame.view, frameParameters );
	// Затенение окружением — по настройкам постобработки (сила, радиус) и её отладочному виду
	m_ambientOcclusion.setSettings( m_postProcess.settings().ambientOcclusion, m_postProcess.showAmbientOcclusion() );
	const RenderFrame renderFrame{ frame.view, m_postProcess.ev100(), depthPrepass };
	for( RenderFeature* feature : m_features )
		feature->beginFrame( renderFrame, frameParameters );
	m_constants.beginFrame( frameParameters );
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

void Renderer::buildCommands( const MeshCollector& collector, std::vector<DrawCommand> ( &commands )[scenePassCount],
							  bool depthPrepass, bool& sceneColorRead, bool& sceneDepthRead )
{
	for( auto& passCommands : commands )
		passCommands.clear();
	sceneColorRead = false;
	sceneDepthRead = false;

	const std::vector<MeshBatch>& meshes = collector.meshes();
	for( uint32_t i = 0; i < meshes.size(); ++i )
	{
		const MeshBatch& batch = meshes[i];
		const MeshPass pass = passFor( batch.state.blendMode, batch.material->readsSceneDepth(), depthPrepass );
		uint64_t key;
		if( pass == MeshPass::transparent )
		{
			key = transparentSortKey( batch.distance, batch.ownerOrder, i );
			sceneColorRead |= batch.material->readsSceneColor();
		}
		else
		{
			const bool prepass = depthPrepass && inDepthPrepass( batch );
			key = opaqueKey( batch, batch.material->phaseFor( *batch.params, meshPhaseOptions( batch, false, prepass ) ) );
			if( prepass )
			{
				const uint32_t prepassIndex = static_cast<uint32_t>( MeshPass::depthPrepass );
				const int depthPhase = batch.material->depthPhaseFor( *batch.params, meshPhaseOptions( batch, false, false ) );
				commands[prepassIndex].push_back( { opaqueKey( batch, depthPhase ), i, false } );
			}
		}
		commands[static_cast<uint32_t>( pass )].push_back( { key, i, false } );
	}

	const std::vector<CustomBatch>& customs = collector.customs();
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
								 transparentSortKey( batch.distance, batch.ownerOrder, static_cast<uint32_t>( meshes.size() ) + i ) :
								 customSortKey( batch.ownerOrder );
			commands[pass].push_back( { key, i, true } );
			sceneColorRead |= transparent && batch.readsSceneColor;
			sceneDepthRead |= transparent && batch.readsSceneDepth;
		}
	}

	for( auto& passCommands : commands )
		std::stable_sort( passCommands.begin(), passCommands.end(),
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
		m_shadowCommands.push_back( { shadowSortKey( batch.ownerOrder, batch.materialId, phase, batch.instanceGroup ), i, false } );
	}

	const std::vector<CustomBatch>& customs = m_shadowCollector.customs();
	for( uint32_t i = 0; i < customs.size(); ++i )
	{
		if( customs[i].passMask & passBit( MeshPass::csmShadowDepth ) )
			m_shadowCommands.push_back( { customSortKey( customs[i].ownerOrder ), i, true } );
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

	// Время — областью на каскад и в ней по объектам под именами «Shadow: <объект>»: в строке «GPU average» они складываются
	// по всем каскадам (время объекта в тенях), а имена объектов без приставки остаются за проходами сцены
	m_gpuProfiler.beginScope( "Shadow depths" );
	for( uint32_t cascade = 0; cascade < ShadowCascades::cascadeCount; ++cascade )
	{
		const RenderView& view = m_shadows.cascadeView( cascade );
		auto step = std::chrono::high_resolution_clock::now();
		collect( scene, view, m_shadowCollector );
		m_stats.add( "Collect shadow views = %.3f ms", millisecondsSince( step ) );
		step = std::chrono::high_resolution_clock::now();
		buildShadowCommands();
		m_stats.add( "Build shadow commands = %.3f ms", millisecondsSince( step ) );

		m_constants.setViewBuffer( view );
		m_shadows.beginCascade( cascade );
		m_gpuProfiler.beginScope( "Shadow cascade " + std::to_string( cascade ) );
		const RenderContext context{ view, MeshPass::csmShadowDepth, RasterState::csmShadowDepth,
									 m_constants, m_vertexPool };
		executeCommands( m_shadowCollector, m_shadowCommands, context, true, "Shadow: " );
		m_gpuProfiler.endScope();
	}
	// Дальний каскад — дальше Dynamic Shadow Distance на всю карту: только лес (импостеры), рельеф и модели уровня в
	// нём не рисуются (RenderView::Kind::farShadow): тень рельефа вдали — карта края тени (TerrainSunShadow)
	if( m_shadows.farActive() )
	{
		const RenderView& view = m_shadows.farView();
		auto step = std::chrono::high_resolution_clock::now();
		collect( scene, view, m_shadowCollector );
		m_stats.add( "Collect shadow views = %.3f ms", millisecondsSince( step ) );
		step = std::chrono::high_resolution_clock::now();
		buildShadowCommands();
		m_stats.add( "Build shadow commands = %.3f ms", millisecondsSince( step ) );
		m_constants.setViewBuffer( view );
		m_shadows.beginFarCascade();
		m_gpuProfiler.beginScope( "Shadow far" );
		const RenderContext context{ view, MeshPass::csmShadowDepth, RasterState::csmShadowDepth, m_constants, m_vertexPool };
		executeCommands( m_shadowCollector, m_shadowCommands, context, true, "Shadow: " );
		m_gpuProfiler.endScope();
	}
	m_gpuProfiler.endScope();

	// Проходы сцены — снова с главного вида
	m_constants.setViewBuffer( frame.view );
	m_stats.add( "Shadow depths = %.3f ms", millisecondsSince( start ) );
}

void Renderer::executePass( MeshPass pass, const RenderView& view, RasterState frameRaster, bool depthFromPrepass,
						   const SceneCapture* capture )
{
	// Проход не полагается на состояние, оставленное прошлым: своя цель, область вывода, без чужих ресурсов.
	// Глубину и блендинг задаёт ScopedRenderState кадра и прохода, растеризатор команд восстанавливается после прохода.
	static const char* const passNames[] = { "Pass depth prepass", "Pass opaque", "Pass opaque depth read", "Pass sky",
											 "Pass reflection prepass", "Pass transparent" };
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
	// Поверхности с экранными отражениями — в свои цели (глубина поверхности и нормаль без ряби)
	if( pass == MeshPass::reflectionPrepass )
		desc = m_reflections.surfacePass();
	// Съёмка сцены — в свои цели (глубины для шейдеров и копии цвета у неё нет: проходы opaque и sky)
	if( capture )
	{
		desc.colors = { { capture->color, "capture color" } };
		desc.depth = PassDesc::Target{ capture->depth, "capture depth" };
		desc.width = capture->width;
		desc.height = capture->height;
		desc.reads = { { &m_shadows.shaderView(), "shadow map" } };
	}
	d3d.beginPass( desc );
	if( depthRead )
		d3d.setSRV( SLOT_SCENE_DEPTH, m_sceneTargets.depthView() );
	if( sceneColorRead )
		d3d.setSRV( SLOT_SCENE_COLOR, m_sceneTargets.colorCopyView() );
	ScopedRenderState passState;
	const RenderContext context{ view, pass, frameRaster, m_constants, m_vertexPool, depthFromPrepass };
	if( capture )
	{
		// Съёмка — одной областью времени на всю (renderCapture): областей в кадре не больше GpuProfiler::maxScopes
		executeCommands( m_captureCollector, m_captureCommands[static_cast<uint32_t>( pass )], context, false );
		return;
	}
	m_gpuProfiler.beginScope( passNames[static_cast<int>( pass )] );
	// Объекты depth prepass — под именами «Prepass: <объект>», как в тенях, поверхности отражений — «Reflection: <объект>»:
	// имена без приставки — их проходы цвета
	executeCommands( m_collector, m_commands[static_cast<uint32_t>( pass )], context, true,
					 pass == MeshPass::depthPrepass ? "Prepass: " : pass == MeshPass::reflectionPrepass ? "Reflection: " : "" );
	m_gpuProfiler.endScope();
}

void Renderer::executeCommands( const MeshCollector& collector, const std::vector<DrawCommand>& commands,
								const RenderContext& context, bool measureOwners, const char* ownerPrefix )
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
			measure( ownerPrefix + owner->name(), run );
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
	// Стадо моделей со скелетом — один буфер матриц скиннинга на всех (sameMesh), первая кость — у экземпляра
	if( batch.skin )
		batch.skin->bind();

	for( size_t chunk = first; chunk <= last; chunk += maxInstancesPerDraw )
	{
		const size_t count = std::min<size_t>( last - chunk + 1, maxInstancesPerDraw );
		m_instanceTransforms.clear();
		for( size_t i = chunk; i < chunk + count; ++i )
		{
			// HLSL читает матрицы по столбцам — транспонирование, как у константного буфера объекта
			const MeshBatch& instance = meshes[commands[i].index];
			InstanceTransform transform;
			transform.world = XMMatrixTranspose( instance.world );
			transform.worldInverseTranspose = XMMatrixTranspose( normalMatrix( instance.world ) );
			transform.boneOffset = instance.boneOffset;
			m_instanceTransforms.push_back( transform );
		}
		m_instanceBuffer.updateData( m_instanceTransforms.data(), count * sizeof( InstanceTransform ) );
		m_instanceBuffer.setToSlot( SLOT_INSTANCE_DATA );
		d3d.drawIndexedInstanced( batch.indexCount, static_cast<uint32_t>( count ), batch.indexOffset, batch.vertexOffset );
		countMeshes( context, 0, 1 );
	}
	countMeshes( context, static_cast<uint32_t>( last - first + 1 ), 0 );
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
	context.constants.setPerObjectBuffer( batch.world, batch.lodDither, batch.boneOffset );
	if( batch.skin )
		batch.skin->bind();
	d3d.drawIndexed( batch.indexCount, batch.indexOffset, batch.vertexOffset );
	countMeshes( context, 1, 1 );
}

void Renderer::countMeshes( const RenderContext& context, uint32_t meshes, uint32_t draws )
{
	// Съёмка сцены — не в счётчиках кадра
	const MeshPass pass = context.pass;
	if( context.view.kind == RenderView::Kind::capture )
		return;
	if( pass == MeshPass::csmShadowDepth )
	{
		m_shadowMeshCount += meshes;
		m_shadowDraws += draws;
	}
	else if( pass != MeshPass::depthPrepass && pass != MeshPass::reflectionPrepass )
	{
		m_meshCount += meshes;
		m_meshDraws += draws;
	}
}

}
