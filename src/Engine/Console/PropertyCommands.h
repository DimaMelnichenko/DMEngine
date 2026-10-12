#pragma once

#include <functional>
#include <string>
#include <vector>
#include "ConsoleCommands.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

// Нажать кнопку окна (как в Details, с историей правок): false — нет такой; name — имя правки
using PressButton = std::function<bool( PropertyContainer& container, const std::string& button, std::string* name )>;

// Команды list / get / set над свойствами окон GUI (PropertyContainer из GUI::addPropertyWatching), как свойства
// объектов в Remote Control API UE: путь — «окно/подокно/свойство», значения — true / false, числа, «x,y,z».
// Объекты читают свойства каждый кадр, поэтому set действует так же, как ползунок в GUI. press — кнопка окна
// («окно/подокно/кнопка», как функции CallInEditor в Remote Control): нажимает её press
void registerPropertyCommands( ConsoleCommands& commands, std::function<const std::vector<PropertyContainer*>&()> roots,
							   PressButton press );

}
