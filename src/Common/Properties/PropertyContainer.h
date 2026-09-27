#pragma once

#include "Property.h"
#include <unordered_map>
#include <type_traits>
#include <vector>


class PropertyContainer
{
public:
	 using PropertyMap = std::unordered_map<std::string, Property>;
public:
    PropertyContainer( const std::string& name = "MUST BE NAMED!!!" );
    virtual ~PropertyContainer();

        
	const Property& property( const std::string& name ) const;
	Property& property( const std::string& name );
	
	// Свойство с таким именем уже есть — возвращается оно, значение не меняется
	template<class TYPE>
	Property* insert( const std::string& name, const TYPE& value )
	{
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

private:
    PropertyMap m_propertyMap;
	std::vector<std::string> m_order;
	std::string m_name;
	std::vector<PropertyContainer*> m_subContainer;
};



