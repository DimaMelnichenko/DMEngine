#pragma once

#include <functional>
#include <vector>
#include "ConsoleCommands.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

// Команды list / get / set над свойствами окон GUI (PropertyContainer из GUI::addPropertyWatching), как свойства
// объектов в Remote Control API UE: путь — «окно/подокно/свойство», значения — true / false, числа, «x,y,z».
// Объекты читают свойства каждый кадр, поэтому set действует так же, как ползунок в GUI
void registerPropertyCommands( ConsoleCommands& commands, std::function<const std::vector<PropertyContainer*>&()> roots );

}
