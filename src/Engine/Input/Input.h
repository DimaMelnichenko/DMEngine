#pragma once

#include <windows.h>
#include <cstdint>
// Только скан-коды клавиш DIK_* (как в DirectInput): сам DirectInput не используется
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "KeyEventNotifier.h"

// Клавиатура и мышь — Raw Input (WM_INPUT, как ввод в UE на Windows): синглтон, как DMD3D::instance(); destroy() — при
// выходе (DMSystem::Shutdown). Состояние клавиш — по скан-кодам DIK_*, мышь — относительные смещения за кадр. Обычные
// сообщения окна (ImGui, ввод текста, сочетания, раскладка, Alt+Tab) не перехватываются; ввод приходит только
// активному окну, при потере фокуса всё отпускается
class Input
{
public:
	static Input& instance();
	static void destroy();
	~Input();

	bool Initialize( HINSTANCE, HWND, int, int );
	// Сообщение окна (DMSystem::wndProc, до DefWindowProc): WM_INPUT — клавиши и мышь, потеря фокуса — всё отпущено
	void handleMessage( UINT message, WPARAM wParam, LPARAM lParam );
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
	bool isRightMouseDown() const { return m_rightButton; }
	// Клавиша зажата (скан-код DIK_*); при вводе текста в редакторе — нет
	bool isKeyDown( uint8_t key ) const { return ( m_keyboardState[key] & 0x80 ) != 0; }

private:
	Input( const Input& ) = delete;
	Input();
	// Все клавиши и кнопки отпущены, смещения — нуль (окно потеряло фокус: иначе клавиши залипали бы после Alt+Tab)
	void clear();

private:
	uint8_t m_keys[256] = {};			// зажатые клавиши по WM_INPUT
	uint8_t m_keyboardState[256] = {};	// состояние кадра (Frame): m_keys или нули при вводе текста
	bool m_rightButton = false;
	long m_mouseDeltaX = 0;				// смещения мыши с прошлого Frame
	long m_mouseDeltaY = 0;

	double m_mouseX = 0.0;
	double m_mouseY = 0.0;

	KeyEventNotifier m_keyNotifier;
	bool m_keyboardBlocked = false;
	bool m_mouseCapture = true;
};
