#pragma once

#include <functional>
#include <string>
#include <vector>
#include "Properties/PropertyContainer.h"

namespace GS
{
class SceneObject;
}

// Общее для панелей редактора (GUI)
namespace Editor
{

// Раздел Outliner: окружение уровня (свет, небо, туман, ветер), объекты сцены, настройки рендера
enum class Category
{
	environment,
	scene,
	rendering
};

// Окно свойств в Outliner: свойства, раздел и объект сцены (у него — флажок видимости); nullptr — не объект сцены.
// saved — правки окна пишет в базу «Save level»; у остальных они живут до выхода
struct Entry
{
	PropertyContainer* properties = nullptr;
	Category category = Category::rendering;
	GS::SceneObject* object = nullptr;
	bool saved = false;
};

// Пункт меню File — действие (кнопка): сохранение, выход; его же вызывает команда action удалённого управления
struct Action
{
	std::string name;
	std::string shortcut;	// подпись в меню
	std::function<void()> run;
};

// Пресеты окружения уровня — подменю File → Preset (LevelPresets): список и активный, выбор и «Save preset as» — у Engine
struct PresetMenu
{
	std::function<std::vector<std::string>()> names;		// пресеты уровня
	std::function<std::string()> active;					// активный; пусто — строки уровня («Level»)
	std::function<bool()> locked;							// активный заблокирован: «Save level» его не пишет
	std::function<void( const std::string& )> select;		// выбрать; пусто — строки уровня
	std::function<void( const std::string& )> saveAs;		// окружение в окнах — новый пресет
};

// Пункт меню View — переключатель с состоянием (каркас, видимость террейна, расстановка)
struct Toggle
{
	std::string name;
	std::string shortcut;
	std::function<bool()> state;
	std::function<void()> toggle;
	bool debugView = false;	// в подменю View → Debug views (раскраска LOD, каскады, вода)
};

}
