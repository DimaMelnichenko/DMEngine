#pragma once


//////////////
// INCLUDES //
//////////////
#include <windows.h>
#include <unordered_map>
#include <memory>

///////////////////////
// MY  INCLUDES //
///////////////////////

#include "Utils\DMTimer.h"

#include "System.h"

#include "Config\Config.h"
#include "ObjectLibrary\LibraryLoader.h"
#include "GUI\GUI.h"
#include "Scene\Scene.h"
#include "Renderer.h"

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
	void beforeExit();


	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:
	// Кадр: сначала Scene::update() меняет состояние сцены на CPU, затем Render() только отправляет команды GPU
	bool Render( const FrameContext& frame );

	void bindingKeys();

private:
	float m_screenWidth;
	float m_screenHeight;
	HWND m_hwnd;

	std::unordered_map<std::string, DMCamera> m_cameraPool;

	DMTimer m_timer;
	Config m_config;
	LibraryLoader m_library;

	Scene m_scene;
	GUI m_GUI;
	Renderer m_renderer;

	bool m_cursorMode = false;
	bool m_wireframe = false;
	bool m_showGUI = true;
	// Время отрисовки GUI в прошлом кадре, мкс: текущее станет известно только после GUI
	uint64_t m_guiRenderTime = 0;
};

}
