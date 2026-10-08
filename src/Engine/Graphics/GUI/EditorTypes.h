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

// Раздел Outliner: окружение уровня (то, что пишет «Save level environment»), объекты сцены, настройки рендера
enum class Category
{
	environment,
	scene,
	rendering
};

// Окно свойств в Outliner: свойства, раздел и объект сцены (у него — флажок видимости); nullptr — не объект сцены
struct Entry
{
	PropertyContainer* properties = nullptr;
	Category category = Category::rendering;
	GS::SceneObject* object = nullptr;
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
};

}
