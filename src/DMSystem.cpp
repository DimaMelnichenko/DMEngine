#include "DMSystem.h"
#include <algorithm>
#include "Engine\Input\Input.h"
#include "Common\DBConnector.h"
#include "Logger\Logger.h"
#include "Resources\resource.h"


DMSystem::DMSystem(  )
{

}

DMSystem::~DMSystem()
{
	Input::destroy();
	ShutdownWindows( );
}

bool DMSystem::Initialize( const char* commandLine )
{
	int16_t screenWidth = 0;
	int16_t	screenHeight = 0;
	bool result = true;

	m_config.readConfig(".\\settings.ini");
	m_config.parseCommandLine( commandLine ? commandLine : "" );

	// Initialize the windows api.
	InitializeWindows( screenWidth, screenHeight );

	if( !DBConnector::instance().init() )
	{
		LOG( "Can`t open base.db3" );
		return false;
	}

	if( !Input::instance().Initialize( m_hinstance, m_hwnd, screenWidth, screenHeight ) )
	{
		LOG( "Can`t initialize Raw Input" );
		return false;
	}

	if( !m_engine.initialize( screenWidth, screenHeight, m_hwnd, m_config ) )
	{
		LOG( "Can`t initialize graphics" );
		return false;
	}

	return true;
}

void DMSystem::Run( )
{
	MSG msg;
	bool quit = false;

	// Initialize the message structure.
	ZeroMemory( &msg, sizeof( MSG ) );
	while( true )
	{
		// Handle all pending windows messages before rendering the next frame.
		while( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
		{
			if( msg.message == WM_QUIT )
			{
				quit = true;
				break;
			}
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}

		// If windows signals to end the application then exit out.
		if( quit || m_engine.exitRequested() )
		{
			m_engine.beforeExit();
			break;
		}

		if( !Input::instance().Frame() )
			break;

		// Frame processing.
		if( !m_engine.tick() )
			break;
	}
}


LRESULT CALLBACK DMSystem::proxyWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	DMSystem* dmSystem;

	if( uMsg == WM_NCCREATE )
	{	
		LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>( lParam );
		dmSystem = static_cast<DMSystem*>( lpcs->lpCreateParams );
		// Put the value in a safe place for future use
		SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( dmSystem ) );

	}
	else
	{
		dmSystem = reinterpret_cast<DMSystem*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );
	}

	if( dmSystem )
	{
		return dmSystem->wndProc( hWnd, uMsg, wParam, lParam );
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

extern LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
LRESULT DMSystem::wndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	// Raw Input (WM_INPUT) и потеря фокуса — до ImGui: он может забрать сообщение себе
	Input::instance().handleMessage( uMsg, wParam, lParam );
	if( ImGui_ImplWin32_WndProcHandler( hWnd, uMsg, wParam, lParam ) )
		return true;

	// Escape — триггер Input, назначенный в Initialize()
	switch( uMsg )
	{
		case WM_DESTROY:
			PostQuitMessage( 0 );
			return 0;			
		case WM_CLOSE:
			PostQuitMessage( 0 );
			return 0;
		case WM_SYSCOMMAND:
			// Alt или F10 без сочетания — не меню окна: DefWindowProc вошёл бы в цикл меню, и кадры стояли бы до следующей
			// клавиши (меню у окна нет; Alt+F4 и Alt+Tab работают)
			if( ( wParam & 0xFFF0 ) == SC_KEYMENU )
				return 0;
			break;
		case WM_SIZE:
			// Новый размер клиентской области — задний буфер и цели кадра за ним; свёрнутое окно (0 × 0) не трогаем
			if( wParam != SIZE_MINIMIZED )
				m_engine.resize( LOWORD( lParam ), HIWORD( lParam ) );
			return 0;
		case WM_CREATE:
		{
			ShowWindow( hWnd, SW_SHOW );
			SetFocus( hWnd );
			SetForegroundWindow( hWnd );
			// Курсор виден: прячет его только поворот камеры мышью и -nomouse (Engine::updateMouseLook, initialize)
			break;
		}
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

void DMSystem::InitializeWindows( int16_t& screenWidth, int16_t& screenHeight )
{
	WNDCLASSEX wc;
	int posX, posY;

	// Пиксели окна — физические: без этого Windows с масштабом экрана 150 % растягивает окно 1920 × 1080 до 2880 × 1620,
	// больше экрана 2560 × 1440, и картинка размыта; «desktop» — настоящее разрешение монитора
	SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

	// Get the instance of this application.
	m_hinstance = GetModuleHandle( nullptr );

	// Класс и заголовок окна; после загрузки уровня в заголовке — его название (Engine::initialize)
	m_applicationName = "DMEngine";

	// Setup the windows class with default settings.
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = DMSystem::proxyWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = m_hinstance;
	// Иконка приложения (src/Resources/DMEngine.rc): большая — Alt+Tab и панель задач, малая — заголовок окна, своим
	// размером из .ico, а не уменьшением большой
	wc.hIcon = static_cast<HICON>( LoadImage( m_hinstance, MAKEINTRESOURCE( IDI_DMENGINE ), IMAGE_ICON, GetSystemMetrics( SM_CXICON ),
											  GetSystemMetrics( SM_CYICON ), LR_DEFAULTCOLOR ) );
	wc.hIconSm = static_cast<HICON>( LoadImage( m_hinstance, MAKEINTRESOURCE( IDI_DMENGINE ), IMAGE_ICON, GetSystemMetrics( SM_CXSMICON ),
												GetSystemMetrics( SM_CYSMICON ), LR_DEFAULTCOLOR ) );
	wc.hCursor = LoadCursor( nullptr, IDC_ARROW );
	wc.hbrBackground = (HBRUSH)GetStockObject( BLACK_BRUSH );
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = m_applicationName;
	wc.cbSize = sizeof( WNDCLASSEX );

	// Register the window class.
	RegisterClassEx( &wc );

	DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
	int windowWidth = 0;
	int windowHeight = 0;
	if( m_config.desktopResolution() )
	{
		// По умолчанию — окно без рамки на весь основной монитор в его разрешении (borderless, «оконный полноэкранный»
		// режим игр): режим монитора не меняется, Alt+Tab мгновенный, а flip model у окна на весь экран отдаёт кадр
		// монитору без композиции
		MONITORINFO monitor = {};
		monitor.cbSize = sizeof( monitor );
		GetMonitorInfo( MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY ), &monitor );
		posX = monitor.rcMonitor.left;
		posY = monitor.rcMonitor.top;
		windowWidth = monitor.rcMonitor.right - monitor.rcMonitor.left;
		windowHeight = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
		screenWidth = static_cast<int16_t>( windowWidth );
		screenHeight = static_cast<int16_t>( windowHeight );
	}
	else
	{
		// Заданное разрешение (Resolution=1920x1080, -resolution) — обычное окно с заголовком (перетаскивается, не
		// закрывает экран) и такой клиентской областью; размер не меняется, окно — в рабочей области по центру
		screenWidth = static_cast<int16_t>( m_config.windowWidth() );
		screenHeight = static_cast<int16_t>( m_config.windowHeight() );
		style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		RECT frame = { 0, 0, screenWidth, screenHeight };
		AdjustWindowRectExForDpi( &frame, style, FALSE, WS_EX_APPWINDOW, GetDpiForSystem() );
		windowWidth = frame.right - frame.left;
		windowHeight = frame.bottom - frame.top;

		RECT work = {};
		SystemParametersInfo( SPI_GETWORKAREA, 0, &work, 0 );
		posX = std::max<int>( work.left, work.left + ( work.right - work.left - windowWidth ) / 2 );
		posY = std::max<int>( work.top, work.top + ( work.bottom - work.top - windowHeight ) / 2 );
	}

	// Create the window with the screen settings and get the handle to it.
	m_hwnd = CreateWindowEx( WS_EX_APPWINDOW, m_applicationName, m_applicationName, style,
							 posX, posY, windowWidth, windowHeight, nullptr, nullptr, m_hinstance, this );
	return;
}

void DMSystem::ShutdownWindows( )
{
	// Курсор виден и свободен (выход посреди поворота мышью — ClipCursor в Engine::updateMouseLook)
	ClipCursor( nullptr );
	ShowCursor( true );

	// Remove the window.
	DestroyWindow( m_hwnd );
	m_hwnd = 0;

	// Remove the application instance.
	UnregisterClass( m_applicationName, m_hinstance );
	m_hinstance = 0;

	return;
}