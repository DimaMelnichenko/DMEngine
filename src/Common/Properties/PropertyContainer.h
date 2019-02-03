#pragma once

#include "Property.h"
#include <unordered_map>
#include <type_traits>


class PropertyContainer
{
public:
	 using PropertyMap = std::unordered_map<std::string, Property>;
public:
    PropertyContainer( const std::string& name = "MUST BE NAMED!!!" );
    virtual ~PropertyContainer();

        
	const Property& property( const std::string& name ) const;
	Property& property( const std::string& name );
	
	template<class TYPE>
	Property* insert( const std::string& name, const TYPE& value )
	{	
		m_propertyMap.insert( { name, Property( name, value ) } );
		return &m_propertyMap[name];
	}

    size_t count() const;
	void setName( const std::string& name );
	const std::string& name();
    
    bool exists(const std::string& name) const;

	Property& operator[]( const std::string& name );
	const Property& operator[]( const std::string& name ) const;

	void addSubContainer( PropertyContainer* subContainer );
	std::vector<PropertyContainer*>& subContainer();

	PropertyMap& propertyMap();
    
private:
    PropertyMap m_propertyMap;
	std::string m_name;
	std::vector<PropertyContainer*> m_subContainer;
};



