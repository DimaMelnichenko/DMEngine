#pragma once

#include <string>
#include "EditorTypes.h"

// Details, как в UE: свойства выбранной в Outliner записи таблицей — поиск, отметка изменённого после загрузки или
// сохранения, сброс по свойству и целиком
class DetailsPanel
{
public:
	// entry — выбранная запись, nullptr — ничего не выбрано; focus — подокно, которое раскрыть и показать (выбор во
	// вьюпорте), остальные подокна сворачиваются
	void draw( Editor::Entry* entry, bool* open, PropertyContainer* focus = nullptr );

private:
	std::string m_filter;
};
