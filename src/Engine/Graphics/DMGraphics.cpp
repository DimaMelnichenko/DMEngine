#include "DMGraphics.h"
#include <string>
#include "Shaders\Layout.h"
#include "../Input/Input.h"
#include "Pipeline.h"
#include <chrono>
#include "Logger\Logger.h"
#include "Scene\TextureObjects\CustomTexture.h"
#include "Engine/Input/Input.h"

#define TIME_POINT() std::chrono::high_resolution_clock::now()

#define TIME_DIFF( start, end ) std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count()

#define TIME_PRINT( start ) std::to_string( std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - start ).count() / 1000.0 )

#define TIME_CHECK( CHECKED_FUNC, INFO_TEXT ) { \
	std::chrono::high_resolution_clock::time_point start##__LINE__ = std::chrono::high_resolution_clock::now(); \
	CHECKED_FUNC; \
	std::chrono::high_resolution_clock::time_point end##__LINE__ = std::chrono::high_resolution_clock::now(); \
	m_GUI.addCounterInfo( (INFO_TEXT), std::chrono::duration_cast<std::chrono::microseconds>( end##__LINE__ - start##__LINE__ ).count() / 1000.0f ); }

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
	m_renderer( m_GUI )
{

}

DMGraphics::~DMGraphics()
{
	pipelineDestroy();
	System::destroy();
	DMD3D::destroy();
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
	result = DMD3D::instance().Initialize( m_config, hwnd );
	LOG( "Initialize the Direct3D object ms: " + TIME_PRINT( timeStart ) );

	if( !result )
	{
		MessageBox( hwnd, "Could not initialize DirectX 11.", "Error", MB_OK );
		return false;
	}

	// Заглушки создаются до загрузки ресурсов и подставляются вместо всего, что не загрузилось
	RET_FALSE( System::textures().createPlaceholder() );
	RET_FALSE( System::textures().createDefaults() );
	RET_FALSE( System::meshes().createPlaceholder() );

	std::unique_ptr<CustomTexture> custTexture( new CustomTexture( 1000000, "monohromeNoise" ) );
	if( !custTexture->generateTexture() )
	{
		LOG( "Failed gen texture" );
		return false;
	}
	System::textures().insertResource( std::move( custTexture ) );

	RET_FALSE( m_scene.loadResources( m_library, m_config.levelName() ) );

	// Создаем общий буфер вершин и индексов
	RET_FALSE( m_renderer.initialize() );

	LOG( "Create main camera" )
	// Основная камера; стартовое положение — секция [Camera] в settings.ini или параметр -camera
	DMCamera& camera = m_cameraPool["main"];
	camera.Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, 0.1f, 10000.0f );
	camera.SetPosition( m_config.cameraPosition().x, m_config.cameraPosition().y, m_config.cameraPosition().z );
	camera.SetRotation( m_config.cameraRotation().x, m_config.cameraRotation().y, 0.0f );

	m_timer.Initialize();

	Layout layout;
	layout.initLayouts();

	pipeline().init();

	RET_FALSE( m_scene.initialize() );

	for( SceneObject* object : m_scene.objects() )
	{
		if( object->properties() )
			m_GUI.addPropertyWatching( object->properties() );
	}
	m_GUI.addPropertyWatching( m_renderer.postProcessProperties() );

	m_GUI.Initialize( m_hwnd );
	m_showGUI = m_config.showGUI();
	if( !m_config.mouseLook() )
		ShowCursor( FALSE );

	LOG( "Total init ms: " + TIME_PRINT( timeStartInit ) );

	bindingKeys();

	return true;
}

bool DMGraphics::Frame()
{
	m_timer.Frame();
	const float elapsedTime = static_cast<float>( m_timer.GetTime() );

	// Подготовка view, proj матриц
	DMCamera& camera = m_cameraPool["main"];
	// С -nomouse камера не читает мышь, как в режиме курсора, — поворот только из -camera и настроек
	TIME_CHECK( camera.Update( elapsedTime, m_cursorMode || !m_config.mouseLook() ), "Camera Update = %.3f ms" );

	const RenderView mainView = RenderView::fromCamera( camera );
	XMFLOAT3 toSun( 0.0f, -1.0f, 0.0f );	// солнца нет — как ниже горизонта
	if( m_scene.lights().sunLightIndex() >= 0 )
	{
		XMFLOAT3 sunColor;
		m_scene.lights().directionalLight( toSun, sunColor );
	}
	const FrameContext frame{ mainView, elapsedTime, toSun };

	// Сначала состояние сцены на CPU, затем команды GPU
	TIME_CHECK( m_scene.update( frame ), "Scene Update = %.3f ms" );

	return Render( frame );
}

bool DMGraphics::Render( const FrameContext& frame )
{
	m_renderer.render( m_scene, frame, m_wireframe );

	if( m_showGUI )
	{
		m_GUI.addCounterInfo( "GUI Rendering = %.3f ms", m_guiRenderTime / 1000.0f );

		auto guiStart = TIME_POINT();
		m_GUI.Begin();
		m_GUI.printCamera( m_cameraPool["main"] );
		m_GUI.End();
		auto guiFinish = TIME_POINT();
		m_guiRenderTime = TIME_DIFF( guiStart, guiFinish );
	}
	else
	{
		m_GUI.skipFrame();
	}

	DMD3D::instance().EndScene();

	return true;
}

void DMGraphics::beforeExit()
{
	m_library.save();
}

void DMGraphics::bindingKeys()
{
	getInput().notifier().registerTrigger( DIK_Q, [this]( bool value )
	{
		m_wireframe = value;
	} );

	getInput().notifier().registerTrigger( DIK_P, []( bool value )
	{
		//if( value )
		{
			DMD3D::instance().createScreenshot();
		}

	} );

	getInput().notifier().registerTrigger( DIK_1, [this]( bool )
	{
		m_scene.terrain().setVisible( !m_scene.terrain().visible() );
	} );

	// Расстановка (трава, камешки) по умолчанию включена: клавиши переключают текущее состояние всех наборов
	getInput().notifier().registerTrigger( DIK_3, [this]( bool )
	{
		for( const auto& scatterer : m_scene.scatterers() )
			scatterer->setComputeEnabled( !scatterer->computeEnabled() );
	} );

	getInput().notifier().registerTrigger( DIK_4, [this]( bool )
	{
		for( const auto& scatterer : m_scene.scatterers() )
			scatterer->setVisible( !scatterer->visible() );
	} );

	// Как Game View в редакторе UE: кадр без окон интерфейса
	getInput().notifier().registerTrigger( DIK_G, [this]( bool )
	{
		m_showGUI = !m_showGUI;
	} );

	getInput().notifier().registerTrigger( DIK_I, [this]( bool value )
	{
		m_cursorMode = value;
		ShowCursor( value );
	} );
}

}
