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
#include "Utils\utilites.h"
#include "Utils.h"
#include "KeyEventNotifier.h"



// DirectInput — синглтон, как DMD3D::instance(); destroy() — при выходе (DMSystem::Shutdown)
class Input
{
public:
	static Input& instance();
	static void destroy();
	~Input();

	bool Initialize( HINSTANCE, HWND, int, int );
	bool Frame( );

	void GetMouseLocation( double&, double& );
	bool IsLeftStride( );
	bool IsRightStride( );
	bool IsForwarPressed( );
	bool IsBackwardPressed( );
	bool IsUpMove( );
	bool IsDownMove( );

	KeyEventNotifier& notifier();

	// Ввод текста в редакторе: клавиатура для движка — как отпущенная (горячие клавиши и WASD не срабатывают);
	// действует со следующего Frame
	void setKeyboardBlocked( bool blocked ) { m_keyboardBlocked = blocked; }
	// Мышь поворачивает камеру: смещения копятся только тогда — иначе поворот прыгнул бы на всё, что накопилось, пока
	// курсор работал с окнами редактора
	void setMouseCapture( bool capture ) { m_mouseCapture = capture; }
	bool isRightMouseDown() const { return ( m_mouseState.rgbButtons[1] & 0x80 ) != 0; }
	// Клавиша зажата (скан-код DirectInput, DIK_*); при вводе текста в редакторе — нет
	bool isKeyDown( uint8_t key ) const { return ( m_keyboardState[key] & 0x80 ) != 0; }

private:
	bool ReadKeyboard( );
	bool ReadMouse( );
	void ProcessInput( );

	Input( const Input& ) = delete;
	Input();

private:
	com_unique_ptr<IDirectInput8> m_directInput;
	com_input_ptr<IDirectInputDevice8> m_keyboard;
	com_input_ptr<IDirectInputDevice8> m_mouse;

	uint8_t m_keyboardState[256] = {};	// нули до первого захвата: иначе мусор читался бы как нажатия
	DIMOUSESTATE m_mouseState = {};

	double m_screenWidth, m_screenHeight;
	double m_mouseX, m_mouseY;

	KeyEventNotifier m_keyNotifier;
	bool m_keyboardBlocked = false;
	bool m_mouseCapture = true;
};

