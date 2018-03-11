#pragma once


//////////////
// INCLUDES //
//////////////
#include <windows.h>
#include <map>
#include <unordered_map>
#include <memory>

///////////////////////
// MY  INCLUDES //
///////////////////////

#include "..\..\Utils\DMTimer.h"
#include "..\..\Utils\dmcpu.h"
#include "..\..\Utils\dmfps.h"
#include "..\Input\DMInput.h"

#include "System.h"

namespace GS
{

/////////////
// GLOBALS //
/////////////
const bool FULL_SCREEN = false;
const bool VSYNC_ENABLED = false;
const float SCREEN_DEPTH = 2000.0f;
const float SCREEN_NEAR = 1.0f;

class DMGraphics
{
public:
	DMGraphics();
	~DMGraphics();

	bool Initialize( HINSTANCE hinstance, int, int, HWND );

	bool Frame();


	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:
	bool Render();

private:
	float m_screenWidth;
	float m_screenHeight;
	HWND m_hwnd;

	DMInput m_Input;
	//std::unordered_map<std::string, DMCamera> m_cameraPool;
};

}