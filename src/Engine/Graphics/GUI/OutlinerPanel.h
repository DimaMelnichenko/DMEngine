#pragma once

#include <string>
#include <vector>
#include "EditorTypes.h"

// Outliner, как в UE: окна свойств по разделам (окружение, сцена, рендер), поиск по имени, выбор — для Details; у объектов
// сцены — флажок видимости
class OutlinerPanel
{
public:
	// selected — выбранная запись (индекс в entries, −1 — ничего), панель меняет его по клику
	void draw( std::vector<Editor::Entry>& entries, int& selected, bool* open );

private:
	std::string m_filter;
};
