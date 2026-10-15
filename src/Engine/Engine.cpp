#include "Engine.h"
#include "ObjectLibrary\LibraryLoader.h"
#include <ctime>
#include <filesystem>
#include <string>
#include <chrono>
#include "Logger\Logger.h"
#include "D3D\TextureImages.h"
#include "Engine/Input/Input.h"
#include "Scene\SceneCommands.h"
#include "Utils\utilites.h"
#include "Utils\SlowTask.h"
#include "Utils\FrameWatchdog.h"

using namespace DirectX;

#define TIME_POINT() std::chrono::high_resolution_clock::now()

#define TIME_DIFF( start, end ) std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count()

#define TIME_PRINT( start ) std::to_string( std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - start ).count() / 1000.0 )

#define TIME_CHECK( CHECKED_FUNC, INFO_TEXT ) { \
	std::chrono::high_resolution_clock::time_point start##__LINE__ = std::chrono::high_resolution_clock::now(); \
	CHECKED_FUNC; \
	std::chrono::high_resolution_clock::time_point end##__LINE__ = std::chrono::high_resolution_clock::now(); \
	m_frameStats.add( (INFO_TEXT), std::chrono::duration_cast<std::chrono::microseconds>( end##__LINE__ - start##__LINE__ ).count() / 1000.0f ); }

#define RET_FALSE(x) \
{\
	if( !(x) ) \
	{\
		LOG( "Fail!" ); \
		return false; \
	}\
}\

namespace GS
{

Engine::Engine() :
	m_library( std::make_unique<LibraryLoader>() ),
	m_renderer( m_frameStats )
{

}

Engine::~Engine()
{
	FrameWatchdog::instance().stop();
	System::destroy();
	// DMD3D::destroy — в main после DMSystem: члены Engine ещё держат ресурсы GPU
}

bool Engine::initialize( int screenWidth, int screenHeight, HWND hwnd, Config config )
{
	auto timeStartInit = TIME_POINT();
	std::chrono::time_point<std::chrono::high_resolution_clock> timeStart;

	bool result = true;
	m_hwnd = hwnd;
	m_config = config;

	m_screenWidth = static_cast<float>( screenWidth );
	m_screenHeight = static_cast<float>( screenHeight );

	timeStart = TIME_POINT();
	DMD3D::Settings d3dSettings;
	// Задний буфер — клиентская область окна (Resolution в settings.ini, -resolution)
	d3dSettings.backBufferWidth = static_cast<uint32_t>( screenWidth );
	d3dSettings.backBufferHeight = static_cast<uint32_t>( screenHeight );
	d3dSettings.vsync = m_config.vSync();
	d3dSettings.gpuValidation = m_config.gpuValidation();
	d3dSettings.pixCapture = m_config.pixCapture();
	result = DMD3D::instance().initialize( d3dSettings, hwnd );
	LOG( "Initialize the Direct3D object ms: " + TIME_PRINT( timeStart ) );

	if( !result )
	{
		MessageBox( hwnd, "Could not initialize Direct3D 12 (see log.txt).", "Error", MB_OK );
		return false;
	}

	// Экран загрузки: GUI — сразу после устройства, задачи загрузки (SlowTask) рисуют кадр с прогрессом; обработчик
	// снимается в конце Initialize и при выходе с ошибкой
	m_GUI.Initialize( m_hwnd );
	m_loadingTitle = "Loading level " + m_config.levelName();
	m_loadingStart = std::chrono::steady_clock::now();
	SlowTask::setHandler( [this]( const std::string& task, float progress ) { drawLoadingFrame( task, progress ); } );
	struct LoadingScreenGuard
	{
		~LoadingScreenGuard() { SlowTask::setHandler( {} ); }
	} loadingScreenGuard;
	// Части загрузки — по времени (Release, уровень Test): текстуры ~1,8 с, сцена (террейн, русла, вода) ~2,8 с, импостеры
	// ~2 с; в лог — сколько заняла каждая (и шаги дольше 100 мс — drawLoadingFrame)
	SlowTask loading( 6.7f );
	std::string stageName;
	auto stageStart = TIME_POINT();
	auto stage = [&]( float work, const char* name )
	{
		if( !stageName.empty() )
			LOG( "Loading: " + stageName + " ms: " + TIME_PRINT( stageStart ) );
		stageName = name;
		stageStart = TIME_POINT();
		loading.enterProgressFrame( work, name );
	};

	// Заглушки создаются до загрузки ресурсов и подставляются вместо всего, что не загрузилось
	RET_FALSE( System::textures().createPlaceholder() );
	RET_FALSE( System::textures().createDefaults() );
	RET_FALSE( System::meshes().createPlaceholder() );
	RET_FALSE( System::meshes().createDefaults() );

	stage( 1.8f, "Materials, models and textures" );
	RET_FALSE( m_scene.loadResources( *m_library, m_config.levelName() ) );

	// Создаем общий буфер вершин и индексов
	stage( 0.05f, "Renderer" );
	RET_FALSE( m_renderer.initialize( m_scene.level().postProcess, m_scene.level().heightFog, m_config.shadowMapResolution(),
									  m_config.depthPrepass() ) );

	LOG( "Create main camera" )
	// Основная камера: ближняя и дальняя плоскости — ScreenNear / ScreenDepth в settings.ini (от дальней зависят сфера
	// неба и слои воздушной перспективы), стартовое положение — секция [Camera] или параметр -camera
	DMCamera& camera = m_cameraPool["main"];
	camera.Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, m_config.screenNear(), m_config.screenDepth() );
	camera.SetPosition( m_config.cameraPosition().x, m_config.cameraPosition().y, m_config.cameraPosition().z );
	camera.SetRotation( m_config.cameraRotation().x, m_config.cameraRotation().y, 0.0f );

	m_timer.Initialize();

	stage( 2.8f, "Scene" );
	RET_FALSE( m_scene.initialize( *m_library, m_config.particles() ) );
	// Импостеры расстановки — рендером моделей в кадры, до прогрева: их материалы прогреются вместе со всеми
	stage( 2.0f, "Impostors" );
	RET_FALSE( m_renderer.bake( m_scene ) );
	// Пайплайны материалов и объектов для проходов кадра — когда объекты созданы, а свет уровня известен (смещение теней)
	stage( 0.05f, "Pipelines" );
	m_renderer.warmPipelines( m_scene );
	stage( 0.0f, "Editor" );

	// -nowind: растения неподвижны — кадры с одной точки совпадают до пикселя. До регистрации в GUI: выключенный ветер —
	// исходное состояние окна, а не правка
	if( !m_config.wind() )
		m_scene.wind().disable();
	// Outliner: окружение уровня, объекты сцены, настройки рендера. Сохраняемые с уровнем (saved) — окружение и объекты
	// сцены с savedWithLevel: их пишет «Save level»
	using Editor::Category;
	m_GUI.addPropertyWatching( m_scene.lights().properties(), Category::environment, nullptr, true );
	for( SceneObject* object : m_scene.objects() )
	{
		if( object->properties() )
			m_GUI.addPropertyWatching( object->properties(), object->environment() ? Category::environment : Category::scene, object,
									   object->savedWithLevel() );
	}
	// Экземпляры моделей — своё окно: положение сохраняется, а параметры материалов окна «Models» — нет
	m_GUI.addPropertyWatching( m_scene.models().instanceProperties(), Category::scene, nullptr, true );
	// Кнопки нового перед камерой: «Add … light» окна «Lights», подокно «Add» окна «Model instances»
	m_quickAdd.initialize( m_cameraPool["main"], m_scene, m_viewport, m_GUI );
	// Окна возможностей рендерера: окружение уровня (туман, постобработка) пишет «Save level», настройки рендеринга (тени) —
	// до выхода
	for( RenderFeature* feature : m_renderer.features() )
	{
		if( feature->environment() && feature->properties() )
			m_GUI.addPropertyWatching( feature->properties(), Category::environment, nullptr, true );
	}
	m_GUI.addPropertyWatching( m_scene.wind().properties(), Category::environment, nullptr, true );
	m_GUI.addPropertyWatching( m_renderer.properties(), Category::rendering );
	m_GUI.addPropertyWatching( m_walk.properties(), Category::rendering );
	for( RenderFeature* feature : m_renderer.features() )
	{
		if( !feature->environment() && feature->properties() )
			m_GUI.addPropertyWatching( feature->properties(), Category::rendering );
	}
	// Камера — и в Outliner (и командам list / get / set): поле зрения, плоскости, скорость; не сохраняется с уровнем
	m_GUI.addPropertyWatching( &camera.m_properties, Category::rendering );
	// Правки окон, сохраняемых с уровнем, — в его строки (LevelLights, SkyAtmosphere, PostProcessSettings,
	// ExponentialHeightFog, Terrain, ScatterLayers, WaterSimulation, ParticleEmitters…). Первое действие — по Ctrl+S
	m_GUI.addAction( "Save level", "Ctrl+S", [this]
	{
		const bool saved = m_scene.saveLevel( *m_library, m_renderer.postProcessSettings(), m_renderer.fogSettings() );
		LOG( saved ? "Level is saved to base.db3" : "Level is not saved" );
		if( saved )
		{
			m_GUI.markSaved();
			m_GUI.notify( "Level is saved to base.db3" );
		}
		else
			m_GUI.notify( "Level is not saved, see Log", true );
	} );
	// На землю (End, как в UE): выбранный во вьюпорте экземпляр модели
	m_GUI.addAction( "Snap to terrain", "End", [this]
	{
		// Одна правка истории (Ctrl+Z вернёт на прежнюю высоту)
		const int selected = m_viewport.selected();
		PropertyContainer* instance = selected >= 0 ? m_scene.models().instanceProperties( selected ) : nullptr;
		if( instance )
			m_GUI.history().begin( "Snap to terrain: " + instance->name(), Editor::EditHistory::collect( *instance ) );
		const bool snapped = m_scene.models().snapToTerrain( selected, m_scene.terrainPipeline() );
		if( instance )
			m_GUI.history().end();
		if( !snapped )
			m_GUI.notify( "Select a model instance over the terrain first", true );
	} );
	m_GUI.addAction( "Exit", "Esc", [this] { m_exitRequested = true; } );
	// Меню View — те же переключатели, что горячие клавиши (нажатие через KeyEventNotifier: состояние клавиши не сбивается)
	auto press = []( uint8_t key ) { return [key] { Input::instance().notifier().press( key ); }; };
	m_GUI.addToggle( { "Wireframe", "Q", [this] { return m_wireframe; }, press( DIK_Q ) } );
	m_GUI.addToggle( { "Terrain", "1", [this] { return m_scene.terrain().visible(); }, press( DIK_1 ) } );
	m_GUI.addToggle( { "Scatter compute", "3", [this]
	{
		return !m_scene.scatterers().empty() && m_scene.scatterers().front()->computeEnabled();
	}, press( DIK_3 ) } );
	m_GUI.addToggle( { "Scatter drawing", "4", [this]
	{
		return !m_scene.scatterers().empty() && m_scene.scatterers().front()->visible();
	}, press( DIK_4 ) } );
	m_GUI.addToggle( { "Fly mode (mouse look)", "I", [this] { return m_flyMode; }, press( DIK_I ) } );
	m_GUI.addToggle( { "Walk on terrain", "F", [this] { return m_walk.enabled(); }, press( DIK_F ) } );
	m_GUI.addToggle( { "Game view (hide editor)", "G", [this] { return !m_showGUI; }, press( DIK_G ) } );
	// Кривые русел во вьюпорте: точки — гизмо, Insert / Delete (Viewport); русла перестраиваются после правки
	m_GUI.addToggle( { "Stream curves", "", [this] { return m_viewport.streamMode(); },
					   [this] { m_viewport.setStreamMode( !m_viewport.streamMode() ); } } );
	// Кисть слоёв и масок: подокно «Paint» окна террейна
	m_GUI.addToggle( { "Terrain paint", "", [this] { return m_viewport.paintMode(); }, [this]
					   {
						   m_viewport.setPaintMode( !m_viewport.paintMode() );
						   m_scene.terrainPaint().setPreview( m_viewport.paintMode() );
					   } } );
	// Отладочные виды — флажки окон объектов одним списком View → Debug views
	auto debugView = [this]( const char* name, PropertyContainer* properties, const char* property )
	{
		if( !properties || !properties->exists( property ) )
			return;
		m_GUI.addToggle( { name, "", [properties, property] { return ( *properties )[property].data<bool>(); },
						   [properties, property] { ( *properties )[property].setData( !( *properties )[property].data<bool>() ); }, true } );
	};
	debugView( "Terrain LOD", m_scene.terrain().properties(), "Show LOD" );
	debugView( "Water on terrain", m_scene.terrain().properties(), "Show water" );
	debugView( "Wind field", m_scene.terrain().properties(), "Show wind" );
	debugView( "Shadow cascades", m_renderer.shadowProperties(), "Show cascades" );
	debugView( "Ambient occlusion", m_renderer.postProcessProperties(), "Show ambient occlusion" );

	m_showGUI = m_config.showGUI();
	if( !m_config.mouseLook() )
		ShowCursor( FALSE );

	LOG( "Loading: " + stageName + " ms: " + TIME_PRINT( stageStart ) );
	LOG( "Total init ms: " + TIME_PRINT( timeStartInit ) );

	bindingKeys();
	// Команды консоли: свои (камера, снимки, замеры, клавиши, выход), редактора, вьюпорта и подсистем сцены
	registerCommands();
	m_GUI.registerCommands( m_console );
	m_viewport.registerCommands( m_console, m_cameraPool["main"], m_scene, m_GUI );
	registerSceneCommands( m_console, m_scene );
	m_GUI.setCommandNames( m_console.names() );
	// Первые кадры — как смена плана: экспозиция сразу по сцене, а не за секунды от начального значения
	m_renderer.cameraCut();
	if( m_config.remoteControl() )
		m_remote.start();

	// Набор пайплайнов собран (материалы — Renderer::warmPipelines, свои — объекты при инициализации): дальше — «ленивые»
	DMD3D::instance().markPipelinesWarm();
	m_initialized = true;
	// Сторож кадра: шаг кадра дольше 10 с — строка в лог с ходом GPU (зависание видно без отладчика, Tools/engine.py
	// печатает его строки по таймауту команды)
	FrameWatchdog::instance().start( std::chrono::seconds( 10 ), [] { return DMD3D::instance().gpuProgress(); } );
	return true;
}

bool Engine::tick()
{
	// Кадр начинается, когда swap chain готов принять его (ввод и камера ниже берутся с меньшей задержкой), кольцо
	// констант — с чистого участка
	FrameWatchdog& watchdog = FrameWatchdog::instance();
	watchdog.stage( "begin frame", true );
	const auto waitStart = TIME_POINT();
	DMD3D::instance().beginFrame();
	m_frameWorkStart = TIME_POINT();
	m_frameStats.add( "Wait for frame = %.3f ms", TIME_DIFF( waitStart, m_frameWorkStart ) / 1000.0f );
	m_frameStats.add( "Submit and present = %.3f ms", m_presentMilliseconds );
	// Прогрев — первые кадры (пересчёт окружения неба, заполнение очередей) в среднее для лога не входят, как у GPU
	constexpr uint32_t warmupFrames = 60;
	if( ++m_tickCount == warmupFrames )
		m_cpuMeasures.start( 3.0f, [this]( const std::string& line ) { logCpuAverage( line ); } );
	// Команды удалённого управления — до кадра: камера, свойства и клавиши действуют уже в нём
	watchdog.stage( "remote and console commands" );
	m_remote.poll( m_console );
	// Команды консоли редактора — там же, где удалённые: в начале кадра, ответ — в окно Output
	for( const std::string& line : m_GUI.takeConsoleCommands() )
		m_console.execute( line, std::make_shared<ConsoleReply>( [this]( const std::string& text ) { m_GUI.consoleReply( text ); } ) );
	m_console.tick();
	m_framesSinceCut = std::min( m_framesSinceCut + 1, settleFrames );

	watchdog.stage( "camera and lights" );
	m_timer.Frame();
	// Время кадра, мс: по таймеру или фиксированным шагом (timestep)
	const float elapsedTime = m_fixedTimeStep > 0.0f ? m_fixedTimeStep * 1000.0f : static_cast<float>( m_timer.GetTime() );

	// Подготовка view, proj матриц
	DMCamera& camera = m_cameraPool["main"];
	updateMouseLook();
	// Ходьба ставит камеру на землю до её Update (поворот мышью — как в полёте)
	if( m_walk.enabled() )
		m_walk.update( elapsedTime / 1000.0f, camera, m_scene.terrainPipeline() );
	TIME_CHECK( camera.Update( elapsedTime, !m_mouseLook ), "Camera Update = %.3f ms" );

	RenderView mainView = RenderView::fromCamera( camera );
	// При ходьбе LOD террейна — от отрезка «глаза — ноги»: самая мелкая сетка вокруг ног, а не только у глаз
	mainView.lodEyeHeight = m_walk.eyeAboveFeet();
	// Правки источников в GUI и время суток — до кадра: по солнцу считаются тени, расстановка и небо
	m_scene.updateLights( elapsedTime / 1000.0f );
	// Тени — от солнца, ночью — от луны; нет источника теней — направление вниз, как светило под горизонтом
	XMFLOAT3 toShadowLight;
	DMLight::ShadowSettings shadowSettings;
	m_scene.lights().shadowLight( toShadowLight, shadowSettings );
	m_gameTime += elapsedTime / 1000.0;
	const FrameContext frame{ mainView, elapsedTime, static_cast<float>( m_gameTime ), toShadowLight };
	// Перестройка по правкам редактора (кривые русел) — не пока тянут ползунок в окне
	watchdog.stage( "editor changes" );
	if( !m_GUI.itemActive() )
		m_scene.applyEditorChanges();

	// Сначала состояние сцены на CPU, затем команды GPU
	watchdog.stage( "scene update" );
	TIME_CHECK( m_scene.update( frame, m_frameStats ), "Scene Update = %.3f ms" );

	const bool rendered = render( frame );
	// Между кадрами — сообщения окна (DMSystem::Run); перетаскивание окна держит их цикл
	watchdog.stage( "between frames (window messages)" );
	return rendered;
}

bool Engine::render( const FrameContext& frame )
{
	FrameWatchdog& watchdog = FrameWatchdog::instance();
	watchdog.stage( "render" );
	m_renderer.render( m_scene, frame, m_wireframe );
	watchdog.stage( "screenshots" );
	takeScreenshots( false );

	if( m_showGUI )
	{
		m_frameStats.add( "GUI Rendering = %.3f ms", m_guiRenderTime / 1000.0f );
		// Кольцо констант за прошлый кадр: переполнения — кольцо мало (constantRingBytes)
		const ConstantRing::Stats& ring = DMD3D::instance().constantRingStats();
		m_frameStats.add( "Constant ring = %.1f KB", ring.frameBytes / 1024.0f );
		m_frameStats.add( "Constant ring writes = %.0f", static_cast<float>( ring.frameWrites ) );
		m_frameStats.add( "Constant ring wraps = %.0f", static_cast<float>( ring.frameWraps ) );
		m_frameStats.add( "Pipelines = %.0f", static_cast<float>( DMD3D::instance().pipelineCount() ) );
		m_frameStats.add( "Pipelines created lazily = %.0f", static_cast<float>( DMD3D::instance().lazyPipelineCount() ) );
		const DMD3D::VideoMemory memory = DMD3D::instance().videoMemory();
		m_frameStats.add( "Video memory used = %.0f MB", memory.usedBytes / ( 1024.0f * 1024.0f ) );
		m_frameStats.add( "Video memory budget = %.0f MB", memory.budgetBytes / ( 1024.0f * 1024.0f ) );
		m_frameStats.add( "Shader descriptors = %.0f", static_cast<float>( DMD3D::instance().shaderDescriptorCount() ) );
		m_frameStats.add( "Barriers per frame = %.0f", static_cast<float>( DMD3D::instance().barrierCount() ) );
		m_frameStats.add( "Indirect draws per frame = %.0f", static_cast<float>( DMD3D::instance().indirectDrawCount() ) );

		watchdog.stage( "GUI" );
		auto guiStart = TIME_POINT();
		// Проход GUI: задний буфер поверх тонмаппинга
		DMD3D& d3d = DMD3D::instance();
		d3d.beginPass( PassDesc{ "GUI", { { &d3d.backBufferTarget(), "back buffer" } }, {}, d3d.backBufferWidth(), d3d.backBufferHeight() } );
		m_GUI.Begin( m_frameStats, m_cameraPool["main"] );
		// Вьюпорт — в кадре ImGui: выбор кликом по сцене, рамка выбранного; пока камера смотрит мышью — без кликов
		if( !m_mouseLook )
			m_viewport.update( m_cameraPool["main"], m_scene, m_GUI );
		m_GUI.End();
		auto guiFinish = TIME_POINT();
		m_guiRenderTime = TIME_DIFF( guiStart, guiFinish );
	}
	// Счётчики показаны (или кадр без интерфейса) — в замеры CPU, следующий кадр собирает свои
	accumulateCpuMeasures();
	m_frameStats.clear();
	takeScreenshots( true );

	watchdog.stage( "end frame (submit and present)" );
	const auto presentStart = TIME_POINT();
	DMD3D::instance().endFrame();
	m_presentMilliseconds = TIME_DIFF( presentStart, TIME_POINT() ) / 1000.0f;

	return true;
}

void Engine::accumulateCpuMeasures()
{
	if( !m_cpuMeasures.active() )
		return;
	// Весь кадр первым, затем счётчики времени — строки «имя = %.3f ms», кроме времени GPU («GPU …», его усредняет stat gpu);
	// одноимённые (объект в каскадах) складываются
	FrameMeasures::Values values{ { "frame", TIME_DIFF( m_frameWorkStart, TIME_POINT() ) / 1000.0f } };
	const std::string suffix = " = %.3f ms";
	for( const auto& [format, value] : m_frameStats.counters )
	{
		if( format.size() > suffix.size() && format.compare( format.size() - suffix.size(), suffix.size(), suffix ) == 0 &&
			format.rfind( "GPU ", 0 ) != 0 )
			values.push_back( { format.substr( 0, format.size() - suffix.size() ), value } );
	}
	m_cpuMeasures.accumulate( values );
}

void Engine::logCpuAverage( const std::string& line )
{
	LOG( line );
}

void Engine::drawLoadingFrame( const std::string& task, float progress )
{
	// Сообщения окна — при каждом шаге: без них Windows через 5 с помечает окно «не отвечает». Закрыли окно — выход сразу
	// после загрузки (WM_SIZE до конца Initialize не применяется — Engine::resize)
	MSG message;
	while( PeekMessage( &message, nullptr, 0, 0, PM_REMOVE ) )
	{
		if( message.message == WM_QUIT )
		{
			m_exitRequested = true;
			continue;
		}
		TranslateMessage( &message );
		DispatchMessage( &message );
	}

	// Шаги дольше 100 мс — в лог: на что уходит загрузка (и доли SlowTask)
	const auto now = std::chrono::steady_clock::now();
	if( task != m_loadingStep )
	{
		const double stepMs = std::chrono::duration<double, std::milli>( now - m_loadingStepStart ).count();
		if( !m_loadingStep.empty() && stepMs >= 100.0 )
			LOG( "Loading step: " + m_loadingStep + ", ms: " + std::to_string( stepMs ) );
		m_loadingStep = task;
		m_loadingStepStart = now;
	}

	// Кадр — не чаще 30 раз в секунду: он выполняет и ждёт записанные команды загрузки (beginFrame); новая задача — сразу
	const double sinceDrawn = std::chrono::duration<double>( now - m_loadingDrawn ).count();
	if( sinceDrawn < ( task == m_loadingTask ? 1.0 / 30.0 : 1.0 / 120.0 ) )
		return;
	m_loadingTask = task;
	m_loadingDrawn = now;

	DMD3D& d3d = DMD3D::instance();
	d3d.beginFrame();
	d3d.beginPass( PassDesc{ "Loading screen", { { &d3d.backBufferTarget(), "back buffer" } }, {}, d3d.backBufferWidth(), d3d.backBufferHeight() } );
	m_GUI.drawLoadingScreen( m_loadingTitle, task, progress,
							 static_cast<float>( std::chrono::duration<double>( now - m_loadingStart ).count() ) );
	d3d.endFrame();
	d3d.resumeLoading();
}

void Engine::beforeExit()
{
	FrameWatchdog::instance().stop();
	m_library->save();
	// Ответ на quit поток канала дописывает до выхода
	m_remote.stop();
}

void Engine::resize( uint32_t width, uint32_t height )
{
	// Свёрнутое окно — 0 × 0, цели не трогаем; до Initialize (WM_SIZE при создании окна) устройства ещё нет
	if( !m_initialized || width == 0 || height == 0 )
		return;
	DMD3D& d3d = DMD3D::instance();
	if( width == d3d.backBufferWidth() && height == d3d.backBufferHeight() )
		return;
	if( !d3d.resize( width, height ) || !m_renderer.resize() )
	{
		LOG( "Can`t resize the frame to " + std::to_string( width ) + "x" + std::to_string( height ) + ", exiting" );
		m_exitRequested = true;
		return;
	}
	m_screenWidth = static_cast<float>( width );
	m_screenHeight = static_cast<float>( height );
	m_cameraPool["main"].setViewport( m_screenWidth, m_screenHeight );
}

void Engine::takeScreenshots( bool withGui )
{
	if( m_framesSinceCut < settleFrames )
		return;
	for( auto it = m_screenshots.begin(); it != m_screenshots.end(); )
	{
		if( it->withGui != withGui )
		{
			++it;
			continue;
		}
		std::wstring path = it->path;
		if( it->frames > 1 )
		{
			// Серия: номер кадра перед расширением
			wchar_t suffix[16];
			swprintf_s( suffix, L"_%02u", it->taken );
			const size_t dot = path.find_last_of( L'.' );
			path.insert( dot == std::wstring::npos ? path.size() : dot, suffix );
		}
		if( !saveScreenshot( path ) )
		{
			if( it->reply )
				it->reply->error( "can`t save the screenshot" );
			it = m_screenshots.erase( it );
			continue;
		}
		if( ++it->taken < it->frames )
		{
			++it;
			continue;
		}
		if( it->reply )
			it->reply->ok();
		it = m_screenshots.erase( it );
	}
}

bool Engine::saveScreenshot( const std::wstring& path )
{
	DMD3D& d3d = DMD3D::instance();
	std::vector<uint8_t> bytes;
	uint32_t rowPitch = 0;
	if( !d3d.captureBackBuffer( bytes, rowPitch ) )
		return false;
	return GpuImages::saveImage( path, d3d.backBufferWidth(), d3d.backBufferHeight(), DMD3D::backBufferViewFormat, bytes.data(), rowPitch );
}

void Engine::updateMouseLook()
{
	// Как вьюпорт редактора UE: камера смотрит мышью, пока зажата правая кнопка над сценой (не над окнами редактора);
	// в режиме полёта (I) и без редактора (G) — всегда. С -nomouse — никогда: поворот только из -camera и команд
	// Только у активного окна: после Alt+Tab курсор свободен, полёт и ходьба продолжат поворот, когда окно снова активно
	Input& input = Input::instance();
	const bool active = GetForegroundWindow() == m_hwnd;
	const bool rightButton = active && input.isRightMouseDown();
	if( !m_rightButtonLook && rightButton && !( m_showGUI && m_GUI.wantsMouse() ) )
		m_rightButtonLook = true;
	else if( m_rightButtonLook && !rightButton )
		m_rightButtonLook = false;
	const bool look = active && m_config.mouseLook() && ( m_rightButtonLook || m_flyMode || m_walk.enabled() || !m_showGUI );
	if( look != m_mouseLook )
	{
		// Курсор прячется на время поворота и стоит на месте (ClipCursor в точку: смещения даёт Raw Input), затем
		// виден там же; окна редактора мышь не видят
		if( look )
		{
			GetCursorPos( &m_lookCursor );
			// Курсор вне окна (полёт включили, когда мышь была за краем) — в середину окна: клик не уйдёт другому окну
			RECT client;
			GetClientRect( m_hwnd, &client );
			MapWindowPoints( m_hwnd, nullptr, reinterpret_cast<POINT*>( &client ), 2 );
			if( !PtInRect( &client, m_lookCursor ) )
				m_lookCursor = { ( client.left + client.right ) / 2, ( client.top + client.bottom ) / 2 };
			const RECT point = { m_lookCursor.x, m_lookCursor.y, m_lookCursor.x + 1, m_lookCursor.y + 1 };
			ClipCursor( &point );
			ShowCursor( FALSE );
		}
		else
		{
			ClipCursor( nullptr );
			SetCursorPos( m_lookCursor.x, m_lookCursor.y );
			ShowCursor( TRUE );
		}
		m_GUI.setMouseEnabled( !look );
		m_mouseLook = look;
	}
	input.setMouseCapture( look );
	// Ввод текста в окнах редактора — горячие клавиши и WASD молчат
	input.setKeyboardBlocked( m_showGUI && m_GUI.wantsKeyboard() );
}

void Engine::bindingKeys()
{
	Input::instance().notifier().registerTrigger( DIK_Q, [this]( bool value )
	{
		m_wireframe = value;
	} );

	// Снимок — в очередь, как команда screenshot: клавиши обрабатываются до кадра (Input::Frame), а задний буфер можно
	// скопировать только в кадре. Кадр как на экране — с окнами ImGui
	Input::instance().notifier().registerTrigger( DIK_P, [this]( bool )
	{
		ScreenshotRequest request;
		request.path = L"screenshot" + std::to_wstring( m_screenshotCounter++ ) + L".jpg";
		request.withGui = true;
		m_screenshots.push_back( std::move( request ) );
	} );

	Input::instance().notifier().registerTrigger( DIK_1, [this]( bool )
	{
		m_scene.terrain().setVisible( !m_scene.terrain().visible() );
	} );

	// Расстановка (трава, камешки) по умолчанию включена: клавиши переключают текущее состояние всех наборов
	Input::instance().notifier().registerTrigger( DIK_3, [this]( bool )
	{
		for( const auto& scatterer : m_scene.scatterers() )
			scatterer->setComputeEnabled( !scatterer->computeEnabled() );
	} );

	Input::instance().notifier().registerTrigger( DIK_4, [this]( bool )
	{
		for( const auto& scatterer : m_scene.scatterers() )
			scatterer->setVisible( !scatterer->visible() );
	} );

	// Как Game View в редакторе UE: кадр без окон интерфейса
	Input::instance().notifier().registerTrigger( DIK_G, [this]( bool )
	{
		m_showGUI = !m_showGUI;
	} );

	// Режим полёта: камера смотрит мышью всегда, без правой кнопки
	Input::instance().notifier().registerTrigger( DIK_I, [this]( bool value )
	{
		m_flyMode = value;
	} );

	Input::instance().notifier().registerTrigger( DIK_END, [this]( bool )
	{
		m_GUI.runAction( "Snap to terrain" );
	} );

	// Полёт ↔ ходьба по рельефу: включение в воздухе — падение до земли. Без террейна — только полёт
	Input::instance().notifier().registerTrigger( DIK_F, [this]( bool )
	{
		const bool walk = !m_walk.enabled() && m_scene.level().terrain.has_value();
		m_walk.setEnabled( walk, m_cameraPool["main"], walk ? &m_scene.terrainPipeline() : nullptr );
		LOG( walk ? "Walk mode" : "Fly mode" );
	} );
}

}
