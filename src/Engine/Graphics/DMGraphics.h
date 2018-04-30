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

#include "Utils\DMTimer.h"
#include "Utils\dmcpu.h"
#include "Utils\dmfps.h"
#include "..\Input\DMInput.h"

#include "System.h"

#include "Scene\VertexPool.h"
#include "Config\Config.h"


namespace GS
{

/////////////
// GLOBALS //
/////////////

class DMGraphics
{
public:
	DMGraphics();
	~DMGraphics();

	bool Initialize( HINSTANCE hinstance, int, int, HWND, Config );

	bool Frame();


	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:
	bool Render();

private:
	float m_screenWidth;
	float m_screenHeight;
	HWND m_hwnd;

	DMInput m_Input;
	std::unordered_map<std::string, DMCamera> m_cameraPool;
	VertexPool m_vertexPool;
	
	DMTimer m_timer;
	Config m_config;
};

}