#pragma once

#include <windows.h>
#include <memory>
#include "Engine/DMEngineProcessor.h"
#include "Engine\Graphics\DMGraphics.h"
#include "Config\Config.h"


class DMSystem 
{
public:
	DMSystem();
	~DMSystem();
	bool Initialize();
	void Run();

	static LRESULT CALLBACK proxyWndProc( HWND, UINT, WPARAM, LPARAM );
	LRESULT wndProc( HWND, UINT, WPARAM, LPARAM );

private:
	void InitializeWindows( int16_t&, int16_t& );
	void ShutdownWindows();
	DMSystem( const DMSystem& other ) = delete;
	DMSystem& operator=( const DMSystem& ) = delete;

private:
	LPCSTR m_applicationName;
	HINSTANCE m_hinstance;
	HWND m_hwnd;
	DMEngineProcessor m_engineProcessor;
	GS::DMGraphics m_graphics;
	Config m_config;
	bool m_exit;
};