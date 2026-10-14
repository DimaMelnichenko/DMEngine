#pragma once

#include "Property.h"
#include <cassert>
#include <functional>
#include <unordered_map>
#include <type_traits>
#include <vector>


class PropertyContainer
{
public:
	 using PropertyMap = std::unordered_map<std::string, Property>;
	// Итог нажатия кнопки: как отменить и повторить сделанное (история правок редактора; пустые — не отменяется) и имя
	// правки (пусто — «кнопка: окно»)
	struct ButtonAction
	{
		std::function<void()> undo;
		std::function<void()> redo;
		std::string name;
	};
	// Кнопка окна в Details, как функция CallInEditor в UE: действие над объектом окна (добавить, удалить). Панель
	// нажимает её после отрисовки окон кадра: действие может перестроить подокна
	struct Button
	{
		std::string name;
		std::string tooltip;
		std::function<ButtonAction()> run;
	};
public:
    PropertyContainer( const std::string& name = "MUST BE NAMED!!!" );
    virtual ~PropertyContainer();

        
	const Property& property( const std::string& name ) const;
	Property& property( const std::string& name );
	
	// Свойство с таким именем уже есть — возвращается оно, значение не меняется. Имя — без «/» (разделитель пути команд
	// list / get / set) и без единицы: её пишет окно по Property::setUnit
	template<class TYPE>
	Property* insert( const std::string& name, const TYPE& value )
	{
		assert( name.find( '/' ) == std::string::npos && "a property name can't contain '/': it separates the path of set" );
		auto [it, inserted] = m_propertyMap.insert( { name, Property( name, value ) } );
		if( inserted )
			m_order.push_back( name );
		return &it->second;
	}
	void setName( const std::string& name );
	const std::string& name();
    
    bool exists(const std::string& name) const;

	Property& operator[]( const std::string& name );
	const Property& operator[]( const std::string& name ) const;

	void addSubContainer( PropertyContainer* subContainer );
	std::vector<PropertyContainer*>& subContainer();

	// Имена свойств в порядке добавления — так их показывает GUI
	const std::vector<std::string>& names() const;

	void addButton( const std::string& name, const std::string& tooltip, std::function<ButtonAction()> run );
	const std::vector<Button>& buttons() const;
	// nullptr — кнопки с таким именем нет
	const Button* button( const std::string& name ) const;

	// Со всеми подконтейнерами: запомнить нынешние значения сохранёнными, есть ли изменённые после этого, вернуть их
	void markSaved();
	bool modified() const;
	void resetToSaved();
	// Состав отличается от сохранённого (подокно добавили или убрали; объекта окна ещё нет в базе): окно изменено, как
	// после правки свойства, до markSaved. Ставит владелец окна; resetToSaved состав не возвращает
	void setStructureModified( bool modified );
	// Не данные уровня (кисть, переключатели для сравнения): правки окна не делают уровень изменённым (modified — false)
	void setTransient( bool transient ) { m_transient = transient; }
	// Правят ли сейчас какое-то его свойство (Property::editing), со всеми подконтейнерами
	bool editing() const;

private:
    PropertyMap m_propertyMap;
	std::vector<std::string> m_order;
	std::string m_name;
	std::vector<PropertyContainer*> m_subContainer;
	std::vector<Button> m_buttons;
	bool m_structureModified = false;
	bool m_transient = false;
};



