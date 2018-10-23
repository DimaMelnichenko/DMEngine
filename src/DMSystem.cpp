#include "DMSystem.h"
#include "Engine\Input\Input.h"
#include "Common\DBConnector.h"


DMSystem::DMSystem(  )
	: m_exit(false)
{

}

DMSystem::~DMSystem()
{
	destroyInput();
	ShutdownWindows( );
}

bool DMSystem::Initialize( )
{
	int16_t screenWidth = 0;
	int16_t	screenHeight = 0;
	bool result = true;

	m_config.readConfig(".\\settings.ini");

	// Initialize the windows api.
	InitializeWindows( screenWidth, screenHeight );

	if( !dbConnect().init() )
		return false;

	if( !getInput().Initialize( m_hinstance, m_hwnd, screenWidth, screenHeight ) )
		return false;

	if( !m_graphics.Initialize( m_hinstance, screenWidth, screenHeight, m_hwnd, m_config ) )
		return false;

	getInput().notifier().registerTrigger( DIK_ESCAPE, [this]( bool )
	{
		m_exit = true;
	} );

	return true;
}

void DMSystem::Run( )
{
	MSG msg;
	bool result = true;

	// Initialize the message structure.
	ZeroMemory( &msg, sizeof( MSG ) );
	while( true )
	{
		// Handle the windows messages.
		if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}

		// If windows signals to end the application then exit out.
		if( msg.message == WM_QUIT || m_exit )
		{
			m_graphics.beforeExit();
			break;
		}
		else
		{
			if( !getInput().Frame() )
				break;

			// Frame processing.
			if( !m_graphics.Frame() )
			{
				break;
			}
		}

	}

	return ;

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
	if( ImGui_ImplWin32_WndProcHandler( hWnd, uMsg, wParam, lParam ) )
		return true;

	switch( uMsg )
	{
		case WM_KEYDOWN:
		{
			if( wParam == VK_ESCAPE )
			{
				DestroyWindow( hWnd );
				return 0;
			}
		}
		break;
		case WM_DESTROY:
			PostQuitMessage( 0 );
			return 0;			
		case WM_CLOSE:
			PostQuitMessage( 0 );
			return 0;
		case WM_CREATE:
		{
			ShowWindow( hWnd, SW_SHOW );
			SetFocus( hWnd );
			SetForegroundWindow( hWnd );
			ShowCursor( false );
			break;
		}
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

void DMSystem::InitializeWindows( int16_t& screenWidth, int16_t& screenHeight )
{
	WNDCLASSEX wc;
	DEVMODE dmScreenSettings;
	int posX, posY;

	// Get the instance of this application.
	m_hinstance = GetModuleHandle( NULL );

	// Give the application a name.
	m_applicationName = "Engine";

	// Setup the windows class with default settings.
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = DMSystem::proxyWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = m_hinstance;
	wc.hIcon = LoadIcon( NULL, IDI_WINLOGO );
	wc.hIconSm = wc.hIcon;
	wc.hCursor = LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground = (HBRUSH)GetStockObject( BLACK_BRUSH );
	wc.lpszMenuName = NULL;
	wc.lpszClassName = m_applicationName;
	wc.cbSize = sizeof( WNDCLASSEX );

	// Register the window class.
	RegisterClassEx( &wc );

	// Determine the resolution of the clients desktop screen.
	screenWidth = GetSystemMetrics( SM_CXSCREEN );
	screenHeight = GetSystemMetrics( SM_CYSCREEN );

	// Setup the screen settings depending on whether it is running in full screen or in windowed mode.
	if( m_config.fullScreen() )
	{
		// If full screen set the screen to maximum size of the users desktop and 32bit.
		memset( &dmScreenSettings, 0, sizeof( dmScreenSettings ) );
		dmScreenSettings.dmSize = sizeof( dmScreenSettings );
		dmScreenSettings.dmPelsWidth = (unsigned long)screenWidth;
		dmScreenSettings.dmPelsHeight = (unsigned long)screenHeight;
		dmScreenSettings.dmBitsPerPel = 32;
		dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

		// Change the display settings to full screen.
		ChangeDisplaySettings( &dmScreenSettings, CDS_FULLSCREEN );

		// Set the position of the window to the top left corner.
		posX = posY = 0;
	}
	else
	{
		// If windowed then set it to 1024x1024 resolution.
		screenWidth = m_config.screenWidth();
		screenHeight = m_config.screenHeight();

		// Place the window in the middle of the screen.
#ifdef HOME_RUN
		posX = ( GetSystemMetrics( SM_CXSCREEN ) - screenWidth ) / 2;
#else
		posX = 1960;
#endif // HOME_RUN
		posY = ( GetSystemMetrics( SM_CYSCREEN ) - screenHeight ) / 2;
	}

	// Create the window with the screen settings and get the handle to it.
	m_hwnd = CreateWindowEx( WS_EX_APPWINDOW, m_applicationName, m_applicationName,
							 WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP,
							 posX, posY, screenWidth, screenHeight, NULL, NULL, m_hinstance, this );
	return;
}

void DMSystem::ShutdownWindows( )
{
	// Show the mouse cursor.
	ShowCursor( true );

	// Fix the display settings if leaving full screen mode.
	if( m_config.fullScreen() )
	{
		ChangeDisplaySettings( NULL, 0 );
	}

	// Remove the window.
	DestroyWindow( m_hwnd );
	m_hwnd = 0;

	// Remove the application instance.
	UnregisterClass( m_applicationName, m_hinstance );
	m_hinstance = 0;

	return;
}