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
	// Активный пресет окружения уровня (пусто — строки уровня) для заметки у окон пресета; locked — «Save level» его не пишет
	void setPreset( const std::string& name, bool locked )
	{
		m_preset = name;
		m_presetLocked = locked;
	}

private:
	std::string m_filter;
	std::string m_preset;
	bool m_presetLocked = false;
};
