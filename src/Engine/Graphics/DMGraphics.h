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
#include "Engine\Console\ConsoleCommands.h"
#include "Engine\Console\RemoteControlServer.h"

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
	// Команда quit удалённого управления: DMSystem выходит, как по Esc
	bool exitRequested() const { return m_exitRequested; }


	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:
	// Кадр: сначала Scene::update() меняет состояние сцены на CPU, затем Render() только отправляет команды GPU
	bool Render( const FrameContext& frame );

	void bindingKeys();
	// Консольные команды: камера, снимок, замер GPU, свойства GUI, клавиши, выход (docs/remote.md)
	void registerCommands();
	// Снимки, заказанные командой screenshot, — в своей точке кадра: до окон ImGui или после них
	void takeScreenshots( bool withGui );

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

	ConsoleCommands m_console;
	RemoteControlServer m_remote;
	struct ScreenshotRequest
	{
		std::wstring path;
		bool withGui = false;
		ConsoleReplyPtr reply;
	};
	std::vector<ScreenshotRequest> m_screenshots;
	// Кадров после смены плана (старт, команда camera): снимок и замер ждут, пока устоится экспозиция
	uint32_t m_framesSinceCut = 0;
	static constexpr uint32_t settleFrames = 10;
	bool m_exitRequested = false;

	bool m_cursorMode = false;
	bool m_wireframe = false;
	bool m_showGUI = true;
	// Время отрисовки GUI в прошлом кадре, мкс: текущее станет известно только после GUI
	uint64_t m_guiRenderTime = 0;
};

}
