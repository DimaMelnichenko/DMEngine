#pragma once

#include <string>
#include "Properties/PropertyContainer.h"

// Контролы свойств (система свойств, src/Common/Properties) — общие для панелей редактора: таблица «подпись | контрол |
// сброс», как Details panel в UE. Подпись длиннее колонки обрезается, полная — с подсказкой и единицами во всплывающем
// окне; изменённое после загрузки или сохранения отмечено и сбрасывается кнопкой
namespace PropertyWidgets
{

// Подстрока без учёта регистра (латиница); пустой образец подходит ко всему
bool matches( const std::string& text, const std::string& pattern );

// Свойства контейнера и его подконтейнеры (сворачиваемые заголовки); filter — подстрока имени свойства или подконтейнера
void drawContainer( PropertyContainer& container, const std::string& filter );

// Есть ли в контейнере что показать под фильтром
bool hasMatches( PropertyContainer& container, const std::string& filter );

}
