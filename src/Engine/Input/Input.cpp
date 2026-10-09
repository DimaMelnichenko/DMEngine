#include "Input.h"
#include <cstring>
#include <memory>
#include <vector>

namespace
{
	static std::unique_ptr<Input> inputPtr;
}

void Input::destroy()
{
	inputPtr.reset();
}

Input& Input::instance()
{
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

bool Input::Initialize( HINSTANCE, HWND hwnd, int, int )
{
	// Мышь и клавиатура (Generic Desktop: usage 2 и 6) — WM_INPUT окну. Без RIDEV_NOLEGACY: обычные сообщения окна
	// остаются (ImGui, ввод текста, Ctrl+S, раскладка), без RIDEV_INPUTSINK — ввод приходит только активному окну
	RAWINPUTDEVICE devices[2] = {};
	devices[0].usUsagePage = 0x01;
	devices[0].usUsage = 0x02;
	devices[0].hwndTarget = hwnd;
	devices[1].usUsagePage = 0x01;
	devices[1].usUsage = 0x06;
	devices[1].hwndTarget = hwnd;
	return RegisterRawInputDevices( devices, 2, sizeof( RAWINPUTDEVICE ) ) != FALSE;
}

void Input::handleMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
	if( ( message == WM_ACTIVATE && LOWORD( wParam ) == WA_INACTIVE ) || message == WM_KILLFOCUS )
	{
		clear();
		return;
	}
	if( message != WM_INPUT || GET_RAWINPUT_CODE_WPARAM( wParam ) != RIM_INPUT )
		return;

	UINT size = 0;
	GetRawInputData( reinterpret_cast<HRAWINPUT>( lParam ), RID_INPUT, nullptr, &size, sizeof( RAWINPUTHEADER ) );
	alignas( RAWINPUT ) uint8_t local[sizeof( RAWINPUT )];
	std::vector<uint8_t> heap;
	uint8_t* bytes = local;
	if( size > sizeof( local ) )
	{
		heap.resize( size );
		bytes = heap.data();
	}
	if( size == 0 || GetRawInputData( reinterpret_cast<HRAWINPUT>( lParam ), RID_INPUT, bytes, &size, sizeof( RAWINPUTHEADER ) ) != size )
		return;
	const RAWINPUT& input = *reinterpret_cast<const RAWINPUT*>( bytes );

	if( input.header.dwType == RIM_TYPEKEYBOARD )
	{
		const RAWKEYBOARD& keyboard = input.data.keyboard;
		// Скан-код как у DIK_*: расширенные клавиши (префикс E0: стрелки, End, правый Ctrl) — со старшим битом. Префикс
		// E1 (Pause) и служебный Shift (VKey 0xFF) пропускаются
		if( keyboard.VKey == 0xFF || ( keyboard.Flags & RI_KEY_E1 ) || keyboard.MakeCode == 0 )
			return;
		const uint8_t key = static_cast<uint8_t>( ( keyboard.MakeCode & 0x7F ) | ( ( keyboard.Flags & RI_KEY_E0 ) ? 0x80 : 0 ) );
		m_keys[key] = ( keyboard.Flags & RI_KEY_BREAK ) ? 0 : 0x80;
	}
	else if( input.header.dwType == RIM_TYPEMOUSE )
	{
		const RAWMOUSE& mouse = input.data.mouse;
		// Абсолютные координаты (удалённый стол, планшет) поворота не дают — только относительные смещения
		if( !( mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) )
		{
			m_mouseDeltaX += mouse.lLastX;
			m_mouseDeltaY += mouse.lLastY;
		}
		if( mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN )
			m_rightButton = true;
		if( mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP )
			m_rightButton = false;
	}
}

void Input::clear()
{
	memset( m_keys, 0, sizeof( m_keys ) );
	m_rightButton = false;
	m_mouseDeltaX = 0;
	m_mouseDeltaY = 0;
}

bool Input::Frame( )
{
	if( m_keyboardBlocked )
		memset( m_keyboardState, 0, sizeof( m_keyboardState ) );
	else
		memcpy( m_keyboardState, m_keys, sizeof( m_keyboardState ) );

	// Смещения мыши за кадр — в положение поворота только при захвате (поворот камеры мышью)
	if( m_mouseCapture )
	{
		m_mouseX += m_mouseDeltaX;
		m_mouseY += m_mouseDeltaY;
	}
	m_mouseDeltaX = 0;
	m_mouseDeltaY = 0;

	m_keyNotifier.process( m_keyboardState );

	return true;
}

void Input::GetMouseLocation( double& mouseX, double& mouseY )
{
	mouseX = m_mouseX;
	mouseY = m_mouseY;
}

bool Input::IsLeftStride( )
{
	return isKeyDown( DIK_A );
}

bool Input::IsRightStride( )
{
	return isKeyDown( DIK_D );
}

bool Input::IsForwarPressed( )
{
	return isKeyDown( DIK_W );
}

bool Input::IsBackwardPressed( )
{
	return isKeyDown( DIK_S );
}

bool Input::IsUpMove( )
{
	return isKeyDown( DIK_SPACE );
}

bool Input::IsDownMove( )
{
	return isKeyDown( DIK_C );
}

KeyEventNotifier& Input::notifier()
{
	return m_keyNotifier;
}
