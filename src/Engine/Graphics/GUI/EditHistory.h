#pragma once

#include <string>
#include <vector>
#include "Properties/PropertyContainer.h"

namespace Editor
{

// История правок свойств — отмена и повтор (Ctrl+Z / Ctrl+Y), как Transactions в UE. Правка — группа свойств со значениями
// «до» и «после»: контрол панели Details от активации до отпускания, кнопки reset, перетаскивание гизмо экземпляра,
// «на землю» (End). Отмена ставит «до» через систему свойств — объекты об отмене не знают: они читают свойства каждый кадр.
// Свойства хранятся указателями: контейнеры живут весь сеанс (создаются при загрузке уровня); объект, который пересоздаёт
// свой контейнер во время работы, сбрасывает историю (clear). Команда set удалённого управления в историю не попадает
class EditHistory
{
public:
	// Начало правки: значения свойств «до» — нынешние или before (по порядку свойств: гизмо снимает их до своего кадра).
	// Правка уже идёт — она закончится (end) и начнётся новая
	void begin( const std::string& name, const std::vector<Property*>& properties, const std::vector<Property::Value>* before = nullptr );
	// Конец правки: значения «после»; ничего не изменилось — правка не записывается
	void end();
	bool editing() const { return m_editing; }

	bool canUndo() const { return !m_undo.empty() && !m_editing; }
	bool canRedo() const { return !m_redo.empty() && !m_editing; }
	// Имя правки, которую отменит undo / повторит redo — для меню Edit и ответа команд
	const std::string& undoName() const;
	const std::string& redoName() const;
	// false — нечего отменять (или правка ещё идёт); name — имя отменённой или повторённой правки
	bool undo( std::string* name = nullptr );
	bool redo( std::string* name = nullptr );
	void clear();

	// Все свойства контейнера со всеми подконтейнерами — для правок окна целиком («Reset all»)
	static std::vector<Property*> collect( PropertyContainer& container );

private:
	struct Change
	{
		Property* property = nullptr;
		Property::Value before;
		Property::Value after;
	};
	struct Transaction
	{
		std::string name;
		std::vector<Change> changes;
	};
	static constexpr size_t maxTransactions = 200;

	std::vector<Transaction> m_undo;
	std::vector<Transaction> m_redo;
	Transaction m_pending;
	bool m_editing = false;
};

}
