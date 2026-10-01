#include "DMGraphics.h"
#include "ObjectLibrary\LibraryLoader.h"
#include <string>
#include "../Input/Input.h"
#include <chrono>
#include "Logger\Logger.h"
#include "D3D\TextureImages.h"
#include "Engine/Input/Input.h"
#include "Engine\Console\PropertyCommands.h"
#include "Utils\utilites.h"

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

DMGraphics::DMGraphics() :
	m_library( std::make_unique<LibraryLoader>() ),
	m_renderer( m_frameStats )
{

}

DMGraphics::~DMGraphics()
{
	System::destroy();
	// DMD3D::destroy — в main после DMSystem: члены DMGraphics ещё держат ресурсы GPU
}

bool DMGraphics::Initialize( HINSTANCE hinstance, int screenWidth, int screenHeight, HWND hwnd, Config config )
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
	d3dSettings.backBufferWidth = static_cast<uint32_t>( m_config.backBufferWidth() );
	d3dSettings.backBufferHeight = static_cast<uint32_t>( m_config.backBufferHeight() );
	d3dSettings.vsync = m_config.vSync();
	d3dSettings.fullscreen = m_config.fullScreen();
	d3dSettings.gpuValidation = m_config.gpuValidation();
	result = DMD3D::instance().initialize( d3dSettings, hwnd );
	LOG( "Initialize the Direct3D object ms: " + TIME_PRINT( timeStart ) );

	if( !result )
	{
		MessageBox( hwnd, "Could not initialize Direct3D 12 (see log.txt).", "Error", MB_OK );
		return false;
	}

	// Заглушки создаются до загрузки ресурсов и подставляются вместо всего, что не загрузилось
	RET_FALSE( System::textures().createPlaceholder() );
	RET_FALSE( System::textures().createDefaults() );
	RET_FALSE( System::meshes().createPlaceholder() );
	RET_FALSE( System::meshes().createDefaults() );

	RET_FALSE( m_scene.loadResources( *m_library, m_config.levelName() ) );

	// Создаем общий буфер вершин и индексов
	RET_FALSE( m_renderer.initialize( m_scene.level().postProcess, m_config.shadowMapResolution(), m_config.depthPrepass() ) );

	LOG( "Create main camera" )
	// Основная камера: ближняя и дальняя плоскости — ScreenNear / ScreenDepth в settings.ini (от дальней зависят сфера
	// неба и слои воздушной перспективы), стартовое положение — секция [Camera] или параметр -camera
	DMCamera& camera = m_cameraPool["main"];
	camera.Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, m_config.screenNear(), m_config.screenDepth() );
	camera.SetPosition( m_config.cameraPosition().x, m_config.cameraPosition().y, m_config.cameraPosition().z );
	camera.SetRotation( m_config.cameraRotation().x, m_config.cameraRotation().y, 0.0f );

	m_timer.Initialize();

	RET_FALSE( m_scene.initialize() );
	// Импостеры расстановки — рендером моделей в кадры, до прогрева: их материалы прогреются вместе со всеми
	RET_FALSE( m_renderer.bake( m_scene ) );
	// Пайплайны материалов и объектов для проходов кадра — когда объекты созданы, а свет уровня известен (смещение теней)
	m_renderer.warmPipelines( m_scene );

	for( SceneObject* object : m_scene.objects() )
	{
		if( object->properties() )
			m_GUI.addPropertyWatching( object->properties() );
	}
	m_GUI.addPropertyWatching( m_renderer.postProcessProperties() );
	m_GUI.addPropertyWatching( m_renderer.shadowProperties() );
	m_GUI.addPropertyWatching( m_renderer.properties() );
	m_GUI.addPropertyWatching( m_scene.lights().properties() );
	m_GUI.addPropertyWatching( m_scene.wind().properties() );
	// -nowind: растения неподвижны — кадры с одной точки совпадают до пикселя
	if( !m_config.wind() )
		m_scene.wind().disable();
	// Правки света, неба, теней и постобработки — в строки уровня (LevelLights, SkyAtmosphere, PostProcessSettings)
	m_GUI.addAction( "Save level environment", [this]
	{
		const bool saved = m_scene.saveEnvironment( *m_library, m_renderer.postProcessSettings() );
		LOG( saved ? "Level environment is saved to base.db3" : "Level environment is not saved" );
	} );

	m_GUI.Initialize( m_hwnd );
	m_showGUI = m_config.showGUI();
	if( !m_config.mouseLook() )
		ShowCursor( FALSE );

	LOG( "Total init ms: " + TIME_PRINT( timeStartInit ) );

	bindingKeys();
	registerCommands();
	// Первые кадры — как смена плана: экспозиция сразу по сцене, а не за секунды от начального значения
	m_renderer.cameraCut();
	if( m_config.remoteControl() )
		m_remote.start();

	// Набор пайплайнов собран (материалы — Renderer::warmPipelines, свои — объекты при инициализации): дальше — «ленивые»
	DMD3D::instance().markPipelinesWarm();
	m_initialized = true;
	return true;
}

bool DMGraphics::Frame()
{
	// Кадр начинается, когда swap chain готов принять его (ввод и камера ниже берутся с меньшей задержкой), кольцо
	// констант — с чистого участка
	DMD3D::instance().beginFrame();
	// Команды удалённого управления — до кадра: камера, свойства и клавиши действуют уже в нём
	m_remote.poll( m_console );
	m_console.tick();
	m_framesSinceCut = std::min( m_framesSinceCut + 1, settleFrames );

	m_timer.Frame();
	// Время кадра, мс: по таймеру или фиксированным шагом (timestep)
	const float elapsedTime = m_fixedTimeStep > 0.0f ? m_fixedTimeStep * 1000.0f : static_cast<float>( m_timer.GetTime() );

	// Подготовка view, proj матриц
	DMCamera& camera = m_cameraPool["main"];
	// С -nomouse камера не читает мышь, как в режиме курсора, — поворот только из -camera и настроек
	TIME_CHECK( camera.Update( elapsedTime, m_cursorMode || !m_config.mouseLook() ), "Camera Update = %.3f ms" );

	const RenderView mainView = RenderView::fromCamera( camera );
	// Правки источников в GUI и время суток — до кадра: по солнцу считаются тени, расстановка и небо
	m_scene.updateLights( elapsedTime / 1000.0f );
	// Тени — от солнца, ночью — от луны; нет источника теней — направление вниз, как светило под горизонтом
	XMFLOAT3 toShadowLight;
	DMLight::ShadowSettings shadowSettings;
	m_scene.lights().shadowLight( toShadowLight, shadowSettings );
	m_gameTime += elapsedTime / 1000.0;
	const FrameContext frame{ mainView, elapsedTime, static_cast<float>( m_gameTime ), toShadowLight };

	// Сначала состояние сцены на CPU, затем команды GPU
	TIME_CHECK( m_scene.update( frame ), "Scene Update = %.3f ms" );

	return Render( frame );
}

bool DMGraphics::Render( const FrameContext& frame )
{
	m_renderer.render( m_scene, frame, m_wireframe );
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

		auto guiStart = TIME_POINT();
		// Проход GUI: задний буфер поверх тонмаппинга
		DMD3D& d3d = DMD3D::instance();
		d3d.beginPass( PassDesc{ "GUI", { { &d3d.backBufferTarget(), "back buffer" } }, {}, d3d.backBufferWidth(), d3d.backBufferHeight() } );
		m_GUI.Begin( m_frameStats );
		m_GUI.printCamera( m_cameraPool["main"] );
		m_GUI.End();
		auto guiFinish = TIME_POINT();
		m_guiRenderTime = TIME_DIFF( guiStart, guiFinish );
	}
	// Счётчики показаны (или кадр без интерфейса) — следующий кадр собирает свои
	m_frameStats.clear();
	takeScreenshots( true );

	DMD3D::instance().endFrame();

	return true;
}

void DMGraphics::beforeExit()
{
	m_library->save();
	// Ответ на quit поток канала дописывает до выхода
	m_remote.stop();
}

void DMGraphics::resize( uint32_t width, uint32_t height )
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

void DMGraphics::takeScreenshots( bool withGui )
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
			it->reply->error( "can`t save the screenshot" );
			it = m_screenshots.erase( it );
			continue;
		}
		if( ++it->taken < it->frames )
		{
			++it;
			continue;
		}
		it->reply->ok();
		it = m_screenshots.erase( it );
	}
}

bool DMGraphics::saveScreenshot( const std::wstring& path )
{
	DMD3D& d3d = DMD3D::instance();
	std::vector<uint8_t> bytes;
	uint32_t rowPitch = 0;
	if( !d3d.captureBackBuffer( bytes, rowPitch ) )
		return false;
	return GpuImages::saveImage( path, d3d.backBufferWidth(), d3d.backBufferHeight(), DMD3D::backBufferViewFormat, bytes.data(), rowPitch );
}

void DMGraphics::registerCommands()
{
	// Камера как BugItGo в UE: положение и поворот в градусах; смена плана — экспозиция сразу по новому виду
	m_console.registerCommand( "passes", "log the passes of the next frame with their targets, reads and writes (D3D/GpuPass.h)",
							   [this]( const std::vector<std::string>&, const ConsoleReplyPtr& reply )
	{
		DMD3D::instance().logPasses();
		reply->ok( "passes of the next frame are written to log.txt" );
	} );
	m_console.registerCommand( "camera", "[x,y,z[,pitch,yaw]] - move the camera (camera cut); no arguments - current camera",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		DMCamera& camera = m_cameraPool["main"];
		std::string joined;
		for( const std::string& arg : args )
			joined += ( joined.empty() ? "" : "," ) + arg;
		std::vector<std::string> parts;
		str_split( joined, parts, ", " );
		std::vector<float> values;
		for( const std::string& part : parts )
		{
			char* end = nullptr;
			values.push_back( std::strtof( part.c_str(), &end ) );
			if( end == part.c_str() || *end != '\0' )
				return reply->error( "camera: can`t parse " + part );
		}
		if( !values.empty() )
		{
			if( values.size() != 3 && values.size() != 5 )
				return reply->error( "camera: expected x,y,z or x,y,z,pitch,yaw" );
			const XMFLOAT2 rotation = values.size() == 5 ? XMFLOAT2( values[3], values[4] ) : camera.rotation();
			camera.setView( XMFLOAT3( values[0], values[1], values[2] ), rotation.x, rotation.y );
			m_renderer.cameraCut();
			m_framesSinceCut = 0;
		}
		char text[128];
		const XMFLOAT3& position = camera.position();
		const XMFLOAT2 rotation = camera.rotation();
		std::snprintf( text, sizeof( text ), "%g,%g,%g,%g,%g", position.x, position.y, position.z, rotation.x, rotation.y );
		reply->ok( text );
	} );

	m_console.registerCommand( "screenshot", "<file.png|.jpg> [gui] [frames] - back buffer after the frame settles; gui - with "
							   "ImGui windows; frames - that many consecutive frames, file_00, file_01...",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() )
			return reply->error( "screenshot: expected a file name" );
		ScreenshotRequest request{ utf8ToWide( args[0] ), false, reply };
		for( size_t i = 1; i < args.size(); ++i )
		{
			if( args[i] == "gui" )
			{
				request.withGui = true;
				continue;
			}
			const int frames = std::atoi( args[i].c_str() );
			if( frames <= 0 || frames > 1000 )
				return reply->error( "screenshot: expected gui or a number of frames 1..1000, got " + args[i] );
			request.frames = static_cast<uint32_t>( frames );
		}
		m_screenshots.push_back( std::move( request ) );
	} );

	m_console.registerCommand( "timestep", "[seconds|off] - fixed frame time step (as -UseFixedTimeStep in UE); no arguments - current",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( !args.empty() )
		{
			if( args[0] == "off" )
				m_fixedTimeStep = 0.0f;
			else
			{
				const float step = static_cast<float>( std::atof( args[0].c_str() ) );
				if( step <= 0.0f || step > 1.0f )
					return reply->error( "timestep: expected seconds in (0, 1] or off" );
				m_fixedTimeStep = step;
			}
		}
		reply->ok( m_fixedTimeStep > 0.0f ? std::to_string( m_fixedTimeStep ) : "off" );
	} );

	m_console.registerCommand( "stat", "gpu [seconds=3] - average GPU time of the frame and passes, after the frame settles",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() || args[0] != "gpu" )
			return reply->error( "stat: only stat gpu is supported" );
		const float seconds = args.size() > 1 ? static_cast<float>( std::atof( args[1].c_str() ) ) : 3.0f;
		if( seconds <= 0.0f )
			return reply->error( "stat gpu: expected a positive number of seconds" );
		m_console.addFrameTask( [this, reply, seconds]
		{
			if( m_framesSinceCut < settleFrames )
				return false;
			m_renderer.measureGpu( seconds, [reply]( const std::string& line ) { reply->ok( line ); } );
			return true;
		} );
	} );

	// Горячие клавиши (bindingKeys): имя — буква или цифра, как на клавиатуре, или скан-код DirectInput
	m_console.registerCommand( "key", "<letter|digit|scan code> - press a hotkey (G, Q, 1, 3, 4...)",
							   [this]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.empty() )
			return reply->error( "key: expected a key" );
		static const std::pair<const char*, uint8_t> rows[] = { { "1234567890", DIK_1 }, { "QWERTYUIOP", DIK_Q },
																 { "ASDFGHJKL", DIK_A }, { "ZXCVBNM", DIK_Z } };
		int code = -1;
		if( args[0].size() == 1 )
		{
			const char c = static_cast<char>( std::toupper( static_cast<unsigned char>( args[0][0] ) ) );
			for( const auto& [keys, first] : rows )
			{
				if( const char* found = std::strchr( keys, c ) )
					code = first + static_cast<int>( found - keys );
			}
		}
		if( code < 0 )
		{
			char* end = nullptr;
			const long value = std::strtol( args[0].c_str(), &end, 0 );
			if( end != args[0].c_str() && *end == '\0' && value > 0 && value < 256 )
				code = static_cast<int>( value );
		}
		if( code < 0 )
			return reply->error( "key: unknown key " + args[0] );
		if( !Input::instance().notifier().press( static_cast<uint8_t>( code ) ) )
			return reply->error( "key: nothing is bound to " + args[0] );
		reply->ok();
	} );

	m_console.registerCommand( "gui", "on|off - ImGui windows, as the G key", [this]( const std::vector<std::string>& args,
																					 const ConsoleReplyPtr& reply )
	{
		if( args.empty() || ( args[0] != "on" && args[0] != "off" ) )
			return reply->error( "gui: expected on or off" );
		m_showGUI = args[0] == "on";
		reply->ok();
	} );

	m_console.registerCommand( "quit", "- exit the engine normally (log is written to the end)",
							   [this]( const std::vector<std::string>&, const ConsoleReplyPtr& reply )
	{
		m_exitRequested = true;
		reply->ok();
	} );

	registerPropertyCommands( m_console, [this]() -> const std::vector<PropertyContainer*>& { return m_GUI.propertyContainers(); } );
}

void DMGraphics::bindingKeys()
{
	Input::instance().notifier().registerTrigger( DIK_Q, [this]( bool value )
	{
		m_wireframe = value;
	} );

	Input::instance().notifier().registerTrigger( DIK_P, [this]( bool )
	{
		saveScreenshot( L"screenshot" + std::to_wstring( m_screenshotCounter++ ) + L".jpg" );
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

	Input::instance().notifier().registerTrigger( DIK_I, [this]( bool value )
	{
		m_cursorMode = value;
		ShowCursor( value );
	} );
}

}
