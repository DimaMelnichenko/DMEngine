#include "Input.h"


Input& getInput()
{
	static std::unique_ptr<Input> inputPtr;

	if( inputPtr == nullptr )
	{
		inputPtr.reset( new Input() );
	}

	return *inputPtr;
}

Input::Input(  )
{
}

Input::~Input()
{

}

bool Input::Initialize( HINSTANCE hinstance, HWND hwnd, int screenWidth, int screenHeight )
{
	HRESULT result;

	// Store the screen size which will be used for positioning the mouse cursor.
	m_screenWidth = screenWidth;
	m_screenHeight = screenHeight;

	// Initialize the location of the mouse on the screen.
	m_mouseX = 0;
	m_mouseY = 0;

	// Initialize the main direct input interface.
	result = DirectInput8Create( hinstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&m_directInput, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	// Initialize the direct input interface for the keyboard.
	IDirectInputDevice8* inputDevice;
	result = m_directInput->CreateDevice( GUID_SysKeyboard, &inputDevice, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	m_keyboard = make_input_ptr<IDirectInputDevice8>( inputDevice );

	// Set the data format.  In this case since it is a keyboard we can use the predefined data format.
	result = m_keyboard->SetDataFormat( &c_dfDIKeyboard );
	if( FAILED( result ) )
	{
		return false;
	}

	// Set the cooperative level of the keyboard to not share with other programs.
	result = m_keyboard->SetCooperativeLevel( hwnd, DISCL_FOREGROUND | DISCL_EXCLUSIVE );
	if( FAILED( result ) )
	{
		return false;
	}

	// Now acquire the keyboard.
	result = m_keyboard->Acquire( );
	if( FAILED( result ) )
	{	
		return false;
	}

	// Initialize the direct input interface for the mouse.
	result = m_directInput->CreateDevice( GUID_SysMouse, &inputDevice, NULL );
	if( FAILED( result ) )
	{
		return false;
	}

	m_mouse = make_input_ptr<IDirectInputDevice8>( inputDevice );

	// Set the data format for the mouse using the pre-defined mouse data format.
	result = m_mouse->SetDataFormat( &c_dfDIMouse );
	if( FAILED( result ) )
	{
		return false;
	}

	// Set the cooperative level of the mouse to share with other programs.
	result = m_mouse->SetCooperativeLevel( hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE );
	if( FAILED( result ) )
	{
		return false;
	}

	// Acquire the mouse.
	result = m_mouse->Acquire( );
	if( FAILED( result ) )
	{
		return false;
	}

	return true;

}


bool Input::Frame( )
{
	bool result;


	// Read the current state of the keyboard.
	result = ReadKeyboard( );
	if( !result )
	{
		return false;
	}

	// Read the current state of the mouse.
	result = ReadMouse( );
	if( !result )
	{
		return false;
	}

	// Process the changes in the mouse and keyboard.
	ProcessInput( );

	m_keyNotifier.process( m_keyboardState );

	return true;
}

bool Input::ReadKeyboard( )
{
	HRESULT result;


	// Read the keyboard device.
	result = m_keyboard->GetDeviceState( sizeof( m_keyboardState ), (LPVOID)&m_keyboardState );
	if( FAILED( result ) )
	{
		// If the keyboard lost focus or was not acquired then try to get control back.
		if( ( result == DIERR_INPUTLOST ) || ( result == DIERR_NOTACQUIRED ) )
		{
			m_keyboard->Acquire( );
		}
		else
		{
			return false;
		}
	}

	return true;
}

bool Input::ReadMouse( )
{
	HRESULT result;


	// Read the mouse device.
	result = m_mouse->GetDeviceState( sizeof( DIMOUSESTATE ), (LPVOID)&m_mouseState );
	if( FAILED( result ) )
	{
		// If the mouse lost focus or was not acquired then try to get control back.
		if( ( result == DIERR_INPUTLOST ) || ( result == DIERR_NOTACQUIRED ) )
		{
			m_mouse->Acquire( );
		}
		else
		{
			return false;
		}
	}

	return true;
}

void Input::ProcessInput( )
{
	// Update the location of the mouse cursor based on the change of the mouse location during the frame.
	m_mouseX += m_mouseState.lX;
	m_mouseY += m_mouseState.lY;
	/*
	// Ensure the mouse location doesn't exceed the screen width or height.
	if( m_mouseX < 0 )
	{
		m_mouseX = 0;
	}
	if( m_mouseY < 0 )
	{
		m_mouseY = 0;
	}

	if( m_mouseX > m_screenWidth )
	{
		m_mouseX = m_screenWidth;
	}
	if( m_mouseY > m_screenHeight )
	{
		m_mouseY = m_screenHeight;
	}
	*/
	return;

}

bool Input::IsEscapePressed( )
{
	// Do a bitwise and on the keyboard state to check if the escape key is currently being pressed.
	if( m_keyboardState[DIK_ESCAPE] & 0x80 )
	{
		return true;
	}

	return false;
}

void Input::GetMouseLocation( double& mouseX, double& mouseY )
{
	mouseX = m_mouseX;
	mouseY = m_mouseY;
	return;
}

bool Input::IsLeftStride( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_A] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsRightStride( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_D] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsForwarPressed( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_W] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsBackwardPressed( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_S] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsUpMove( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_SPACE] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsDownMove( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_C] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsLookUpPressed( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_UP] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsLookDownPressed( )
{
	// Do a bitwise and on the keyboard state to check if the key is currently being pressed.
	if( m_keyboardState[DIK_DOWN] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsLookLeftPressed()
{	
	if( m_keyboardState[DIK_LEFT] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::IsLookRightPressed()
{
	if( m_keyboardState[DIK_RIGHT] & 0x80 )
	{
		return true;
	}

	return false;
}

bool Input::isKeyPressed( unsigned char _key )
{
	if( m_keyboardState[_key] & 0x80 )
	{
		return true;
	}

	return false;
}

KeyEventNotifier& Input::notifier()
{
	return m_keyNotifier;
}