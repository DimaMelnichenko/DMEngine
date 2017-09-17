#pragma once

//////////////
// INCLUDES //
//////////////
#include <dinput.h>

///////////////////////////////
// PRE-PROCESSING DIRECTIVES //
///////////////////////////////
#define DIRECTINPUT_VERSION 0x0800

/////////////
// LINKING //
/////////////
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

class DMInput
{
public:
	DMInput();
	~DMInput();

	bool Initialize( HINSTANCE, HWND, int, int );
	void Shutdown( );
	bool Frame( );

	void GetMouseLocation( double&, double& );

	bool isKeyPressed( unsigned char );
	bool IsEscapePressed( );
	bool IsLeftStride( );
	bool IsRightStride( );
	bool IsForwarPressed( );
	bool IsBackwardPressed( );
	bool IsUpMove( );
	bool IsDownMove( );
	bool IsLookUpPressed( );
	bool IsLookDownPressed( );
	bool IsLookLeftPressed( );
	bool IsLookRightPressed( );

private:
	bool ReadKeyboard( );
	bool ReadMouse( );
	void ProcessInput( );

	DMInput( const DMInput& ) = delete;

private:
	IDirectInput8* m_directInput;
	IDirectInputDevice8* m_keyboard;
	IDirectInputDevice8* m_mouse;

	unsigned char m_keyboardState[256];
	DIMOUSESTATE m_mouseState;

	double m_screenWidth, m_screenHeight;
	double m_mouseX, m_mouseY;
};

