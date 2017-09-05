#include "DMSystem.h"


DMSystem::DMSystem(  )
{

}

DMSystem::~DMSystem()
{
	ShutdownWindows( );
}

bool DMSystem::Initialize( )
{
	int16_t screenWidth = 0;
	int16_t	screenHeight = 0;
	bool result = true;

	// Initialize the windows api.
	InitializeWindows( screenWidth, screenHeight );

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
		if( msg.message == WM_QUIT )
		{
			break;
		}
		else
		{
			// Otherwise do the frame processing.
			/*if( !m_graphics->Frame() )
			{
				break;
			}*/
		}

	}

	return ;

}

LRESULT CALLBACK DMSystem::proxyWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	DMSystem* dmSystem;

	if( uMsg == WM_NCCREATE )
	{	
		LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>( lParam );
		dmSystem = static_cast<DMSystem*>( lpcs->lpCreateParams );
		// Put the value in a safe place for future use
		SetWindowLongPtr( hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( dmSystem ) );

	}
	else
	{
		dmSystem = reinterpret_cast<DMSystem*>( GetWindowLongPtr( hwnd, GWLP_USERDATA ) );
	}

	if( dmSystem )
	{
		return dmSystem->wndProc( hwnd, uMsg, wParam, lParam );
	}

	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}

LRESULT DMSystem::wndProc( HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam )
{
	switch( umsg )
	{
		case WM_KEYDOWN:
		{
			if( wparam == VK_ESCAPE )
			{
				DestroyWindow( hwnd );
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
			ShowWindow( hwnd, SW_SHOW );
			SetFocus( hwnd );
			SetForegroundWindow( hwnd );
			ShowCursor( false );
			break;
		}
	}

	return DefWindowProc( hwnd, umsg, wparam, lparam );
}

void DMSystem::InitializeWindows( int16_t& screenWidth, int16_t& screenHeight )
{
	WNDCLASSEX wc;
	DEVMODE dmScreenSettings;
	int posX, posY;

	// Get the instance of this application.
	m_hinstance = GetModuleHandle( NULL );

	// Give the application a name.
	m_applicationName = L"Engine";

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
	if( m_config.isFullScreen() )
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
		screenWidth = 400;
		screenHeight = 400;

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
	if( m_config.isFullScreen() )
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