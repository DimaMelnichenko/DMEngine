#pragma once

#include <string>
#include "Properties/PropertyContainer.h"

namespace Editor
{
class EditHistory;
}

// Контролы свойств (система свойств, src/Common/Properties) — общие для панелей редактора: таблица «подпись | контрол |
// сброс», как Details panel в UE. Подпись длиннее колонки обрезается, полная — с подсказкой и единицами во всплывающем
// окне; изменённое после загрузки или сохранения отмечено и сбрасывается кнопкой
namespace PropertyWidgets
{

// Подстрока без учёта регистра (латиница); пустой образец подходит ко всему
bool matches( const std::string& text, const std::string& pattern );

// Цвет окон пресета окружения уровня (PropertyContainer::presetPart; их правки не меняют другие пресеты, в том числе эталон
// проверок): подкраска строк и заголовков подокон в Details, имя в Outliner, заметка в шапке — RGBA
constexpr float presetColor[4] = { 0.35f, 0.75f, 0.85f, 1.0f };

// Числовое поле как в Blender — общее для всех контролов чисел: перетаскивание вбок меняет значение от движения мыши (без
// прыжка к точке клика), клик без движения — ввод числа (Enter — принять, Esc — отменить), стрелки по краям при
// наведении — шаг вниз и вверх, Shift при перетаскивании — вдесятеро точнее, Ctrl — шагами. low < high — границы
// перетаскивания и стрелок (ввод числом — за ними можно); fill — заливка доли диапазона, как у фактор-слайдеров Blender;
// speed — изменение на пиксель (0 — у границ весь диапазон на ширину поля, без них — по величине значения); step — шаг
// стрелок и Ctrl (0 — подбор по speed, Property::setStep). Ширина — как у прочих контролов (SetNextItemWidth); id с «##» —
// без подписи справа
struct NumberOptions
{
	float low = 0.0f;
	float high = 0.0f;
	float speed = 0.0f;
	float step = 0.0f;
	bool fill = false;
	bool logarithmic = false;
};
bool numberField( const char* id, float* value, const char* format, const NumberOptions& options );
bool numberField( const char* id, int32_t* value, const NumberOptions& options );

// Свойства контейнера, его кнопки (PropertyContainer::addButton) и подконтейнеры (сворачиваемые заголовки); filter —
// подстрока имени свойства, кнопки или подконтейнера; focus — подконтейнер, который раскрыть (с родителями) и прокрутить
// к нему, прочие свернуть; nullptr — как есть
void drawContainer( PropertyContainer& container, const std::string& filter, PropertyContainer* focus = nullptr );

// Нажатая в этом кадре кнопка окна: её действие выполняет GUI после отрисовки панелей (оно может перестроить подокна,
// которые рисовались). false — не нажимали; нажатие забирается
bool takePressedButton( PropertyContainer*& container, std::string& button );

// Есть ли в контейнере что показать под фильтром
bool hasMatches( PropertyContainer& container, const std::string& filter );

// Конец кадра GUI: правка свойства, контрол которого в этом кадре не рисовался (окно закрыли, свернули подконтейнер
// посреди перетаскивания), закончена — Property::editing не залипает, запись истории закрыта
void endFrame();

// История правок (Ctrl+Z): контролы пишут в неё правку от активации до отпускания, кнопки reset — сброс. Ставит GUI
void setHistory( Editor::EditHistory* history );
Editor::EditHistory* history();

}
