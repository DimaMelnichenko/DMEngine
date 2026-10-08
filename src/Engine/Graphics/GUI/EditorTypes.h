#pragma once

#include <functional>
#include <string>
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
