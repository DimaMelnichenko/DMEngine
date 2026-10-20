#pragma once
#include <Windows.h>
#include <functional>
#include <string>
#include <vector>
#include "imgui.h"
#include "Properties/PropertyContainer.h"
#include "FrameStats.h"
#include "EditorTypes.h"
#include "OutlinerPanel.h"
#include "DetailsPanel.h"
#include "StatsPanel.h"
#include "ConsolePanel.h"
#include "CameraPanel.h"
#include "TextureLibraryPanel.h"
#include "EditHistory.h"

class DMCamera;
namespace GS
{
class ConsoleCommands;
}

// Редактор поверх сцены, как редактор UE (docs/gui.md): главное меню (File — действия, View — переключатели, Window —
// панели и раскладка, Help — горячие клавиши) и панели — Outliner (окна свойств по разделам), Details (свойства
// выбранного), Output (консоль и лог), Stats, Camera, Texture Library; поверх сцены — оверлей FPS и времени кадра.
// С ImGui ветки docking панели прилипают к краям экрана и складываются вкладками, сцена видна в центре; без неё —
// раскладка фиксированными местами. Раскладка по умолчанию строится кодом, «Window > Reset layout» — заново
class GUI
{
public:
	GUI();
	~GUI();

	void Initialize( HWND hwnd );
	// Окна кадра: stats — счётчики кадра, camera — главная камера (панель Camera)
	void Begin( const GS::FrameStats& stats, DMCamera& camera );
	void End();
	// Кадр экрана загрузки целиком (вместо Begin / End): заголовок, что делается, полоса прогресса 0…1 и секунды с начала
	void drawLoadingScreen( const std::string& title, const std::string& task, float progress, float seconds );

	// Окно свойств: в Outliner — в разделе category; у объекта сцены — флажок видимости; saved — правки пишет в базу
	// «Save level». Значения при регистрации — «сохранённые» (Details отмечает изменённые после них)
	void addPropertyWatching( PropertyContainer* propertyContainer, Editor::Category category = Editor::Category::rendering,
							  GS::SceneObject* object = nullptr, bool saved = false );
	// Окна свойств в порядке регистрации — для команд list / get / set удалённого управления
	const std::vector<PropertyContainer*>& propertyContainers() const { return m_containers; }
	// История правок свойств: Ctrl+Z / Ctrl+Y, меню Edit, команды undo / redo
	Editor::EditHistory& history() { return m_history; }
	// Действие меню File (сохранение, выход); его же вызывает команда action. false — нет такого
	void addAction( const std::string& name, const std::string& shortcut, std::function<void()> run );
	bool runAction( const std::string& name );
	// Нажать кнопку окна свойств (PropertyContainer::addButton): кнопка в Details, команда press, Delete во вьюпорте.
	// Действие с отменой — правка истории (Ctrl+Z); ответ — её имя. false — кнопки с таким именем у окна нет
	bool press( PropertyContainer& container, const std::string& button, std::string* name = nullptr );
	// Отмена и повтор последней правки (Ctrl+Z / Ctrl+Y): false — нечего; name — её имя
	bool undo( std::string* name = nullptr );
	bool redo( std::string* name = nullptr );
	// Выбрать окно свойств в Outliner по имени (команда select — снимки Details сценарием); false — нет такого
	bool select( const std::string& name );
	const std::vector<Editor::Action>& actions() const { return m_actions; }
	const std::vector<Editor::Toggle>& toggles() const { return m_toggles; }
	// В прошлом кадре держали элемент окна (тянут ползунок, набирают число): тяжёлую перестройку по правке — после
	bool itemActive() const { return m_itemActive; }
	// Переключатель меню View
	void addToggle( Editor::Toggle toggle );
	// Подменю File → Preset: пресеты окружения уровня, выбор и «Save preset as» (окно с именем)
	void setPresetMenu( Editor::PresetMenu menu ) { m_presetMenu = std::move( menu ); }
	// Выбрать в Outliner запись, где лежит container (сама или подокно), и раскрыть его в Details (выбор во вьюпорте);
	// false — такого окна нет
	bool focusProperties( PropertyContainer* container );
	// Команды редактора в консоли и удалённом управлении: action, select, toggle, undo, redo и свойства окон — list, get,
	// set, press (GUICommands.cpp)
	void registerCommands( GS::ConsoleCommands& console );
	// Имена консольных команд — для дополнения в консоли (после регистрации всех команд)
	void setCommandNames( std::vector<std::string> names ) { m_commandNames = std::move( names ); }

	// Строки, набранные в консоли, — выполнить в начале кадра; ответ — consoleReply
	std::vector<std::string> takeConsoleCommands() { return m_console.takePending(); }
	void consoleReply( const std::string& text ) { m_console.appendReply( text ); }

	// Нынешние значения окон, сохраняемых с уровнем, — сохранёнными (после «Save level»); есть ли в них несохранённое
	void markSaved();
	bool modified() const;
	// Сообщение в углу экрана на несколько секунд (сохранено, ошибка)
	void notify( const std::string& text, bool error = false );
	// Окно «Exit DMEngine?» (Esc, File → Exit): открыть в следующем кадре; ответ — handler( true ) выйти, ( false ) отмена.
	// При несохранённых правках окон уровня — ещё «Save and quit» (действие «Save level», затем выход)
	void requestExitConfirmation() { m_openExitPopup = true; }
	void setExitHandler( std::function<void( bool quit )> handler ) { m_exitHandler = std::move( handler ); }
	bool exitConfirmationOpen() const { return m_openExitPopup || m_exitPopupOpen; }

	// Курсор над окнами или окно перетаскивается — мышь у GUI, камере не давать; идёт ввод текста — горячие клавиши
	// не срабатывают. По прошлому кадру
	bool wantsMouse() const;
	bool wantsKeyboard() const;
	// Камера смотрит мышью (правая кнопка): GUI не реагирует на мышь
	void setMouseEnabled( bool enabled );

private:
	void applyStyle( HWND hwnd );
	void drawMenu();
	// Раскладка панелей: с докингом — dockspace и узлы по умолчанию, без него — места окон. Возвращает левый верхний угол
	// области сцены (для оверлея)
	void layout( float& sceneX, float& sceneY );
	void placeWindow( const char* name, float x, float y, float width, float height );
	// Перед окном панели: его место по умолчанию (без докинга); всегда true — для условия вызова панели
	bool placeNext( const char* name );
	void drawNotification();
	void drawHotkeys();
	// Окно «Save preset as»: имя нового пресета окружения
	void drawPresetPopup();
	// Окно «Exit DMEngine?»
	void drawExitPopup();

	struct Place
	{
		const char* name;
		ImVec2 position;
		ImVec2 size;
	};
	std::vector<Place> m_places;	// места окон раскладки без докинга, кадра
	std::vector<Editor::Entry> m_entries;
	std::vector<PropertyContainer*> m_containers;
	Editor::EditHistory m_history;
	int m_selected = -1;
	PropertyContainer* m_focus = nullptr;	// раскрыть в Details в следующем кадре (focusProperties)
	std::vector<Editor::Action> m_actions;
	std::vector<Editor::Toggle> m_toggles;
	std::vector<std::string> m_commandNames;
	Editor::PresetMenu m_presetMenu;
	bool m_openPresetPopup = false;		// открыть окно «Save preset as» в этом кадре
	std::string m_presetName;			// имя в нём
	bool m_openExitPopup = false;		// открыть окно выхода в этом кадре
	bool m_exitPopupOpen = false;		// окно выхода на экране
	std::function<void( bool )> m_exitHandler;

	OutlinerPanel m_outliner;
	DetailsPanel m_details;
	StatsPanel m_stats;
	ConsolePanel m_console;
	CameraPanel m_camera;
	TextureLibraryPanel m_textures;

	bool m_showOutliner = true;
	bool m_showDetails = true;
	bool m_showOutput = true;
	bool m_showStats = true;
	bool m_showCamera = true;
	bool m_showTextures = false;
	bool m_showOverlay = true;
	bool m_showHotkeys = false;
	bool m_resetLayout = false;
	bool m_itemActive = false;
	bool m_firstFrame = true;

	std::string m_notice;
	bool m_noticeError = false;
	double m_noticeTime = -100.0;

	bool m_isInited = false;
};
