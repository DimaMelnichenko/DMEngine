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
#include "GUI\GUI.h"
#include "GUI\Viewport.h"
#include "Scene\Scene.h"
#include "Renderer.h"
#include "Engine\Console\ConsoleCommands.h"
#include "Engine\Console\RemoteControlServer.h"

class LibraryLoader;

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
	// WM_SIZE: задний буфер, буфер сцены, цели постобработки и проекция камеры — под новый размер клиентской области
	void resize( uint32_t width, uint32_t height );
	// Команда quit удалённого управления: DMSystem выходит, как по Esc
	bool exitRequested() const { return m_exitRequested; }


	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:
	// Кадр: сначала Scene::update() меняет состояние сцены на CPU, затем Render() только отправляет команды GPU
	bool Render( const FrameContext& frame );

	void bindingKeys();
	// Поворот камеры мышью: правая кнопка над сценой, режим полёта (I), кадр без редактора (G); курсор и мышь GUI
	void updateMouseLook();
	// Консольные команды: камера, снимок, замер GPU, свойства GUI, клавиши, выход (docs/remote.md)
	void registerCommands();
	// Снимки, заказанные командой screenshot, — в своей точке кадра: до окон ImGui или после них
	void takeScreenshots( bool withGui );
	// Задний буфер в файл (PNG или JPG по расширению) — в кадре до endFrame
	bool saveScreenshot( const std::wstring& path );

private:
	float m_screenWidth;
	float m_screenHeight;
	HWND m_hwnd;
	bool m_initialized = false;	// WM_SIZE приходит и при создании окна, до Initialize

	std::unordered_map<std::string, DMCamera> m_cameraPool;

	DMTimer m_timer;
	Config m_config;
	std::unique_ptr<LibraryLoader> m_library;

	Scene m_scene;
	GUI m_GUI;
	Viewport m_viewport;
	FrameStats m_frameStats;	// счётчики окна «Statistic» за кадр: пишут Frame и рендерер, показывает GUI
	Renderer m_renderer;

	ConsoleCommands m_console;
	RemoteControlServer m_remote;
	struct ScreenshotRequest
	{
		std::wstring path;
		bool withGui = false;
		ConsoleReplyPtr reply;	// ответ команде screenshot; у снимка по клавише P — нет
		uint32_t frames = 1;	// серия: столько кадров подряд, файлы <имя>_00, <имя>_01…
		uint32_t taken = 0;
	};
	std::vector<ScreenshotRequest> m_screenshots;
	uint16_t m_screenshotCounter = 0;	// снимки по клавише P: screenshotN.jpg в рабочей папке
	// Кадров после смены плана (старт, команда camera): снимок и замер ждут, пока устоится экспозиция
	uint32_t m_framesSinceCut = 0;
	static constexpr uint32_t settleFrames = 10;
	bool m_exitRequested = false;
	// Фиксированный шаг времени кадра, с (команда timestep, как -UseFixedTimeStep в UE): время камеры, экспозиции,
	// частиц и суток идёт шагами, сколько бы кадр ни длился, — серия снимков не сбивается записью файлов. 0 — таймер
	float m_fixedTimeStep = 0.0f;
	double m_gameTime = 0.0;	// время игры, с: FrameContext::gameTime, шейдерам — cb_gameTime

	bool m_flyMode = false;			// I: камера смотрит мышью всегда
	bool m_rightButtonLook = false;	// зажата правая кнопка, нажатая над сценой
	bool m_mouseLook = false;		// камера читает мышь в этом кадре
	POINT m_lookCursor = {};		// где был курсор до поворота — туда он возвращается
	bool m_wireframe = false;
	bool m_showGUI = true;
	// Время отрисовки GUI в прошлом кадре, мкс: текущее станет известно только после GUI
	uint64_t m_guiRenderTime = 0;
};

}
