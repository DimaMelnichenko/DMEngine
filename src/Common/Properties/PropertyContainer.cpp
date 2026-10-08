#include "PropertyContainer.h"
#include <functional>


//--------------------------------------------
// PropertyContainer

PropertyContainer::PropertyContainer( const std::string& name ) : m_name( name )
{
}

PropertyContainer::~PropertyContainer()
{

}

const Property& PropertyContainer::property( const std::string& name ) const
{
	return m_propertyMap.at(name);
}

Property& PropertyContainer::property( const std::string& name )
{
	return m_propertyMap.at( name );
}

bool PropertyContainer::exists( const std::string& name ) const
{
	return (bool)m_propertyMap.count( name );
}

Property& PropertyContainer::operator[]( const std::string& name )
{
	return property( name );
}

const Property& PropertyContainer::operator[]( const std::string& name ) const
{
	return property( name );
}

const std::string& PropertyContainer::name()
{
	return m_name;
}

void PropertyContainer::setName( const std::string& name )
{
	m_name = name;
}

std::vector<PropertyContainer*>& PropertyContainer::subContainer()
{
	return m_subContainer;
}

void PropertyContainer::addSubContainer( PropertyContainer* subContainer )
{
	m_subContainer.push_back( subContainer );
}

const std::vector<std::string>& PropertyContainer::names() const
{
	return m_order;
}

void PropertyContainer::markSaved()
{
	for( auto& [name, property] : m_propertyMap )
		property.markSaved();
	for( PropertyContainer* sub : m_subContainer )
		sub->markSaved();
}

bool PropertyContainer::modified() const
{
	for( const auto& [name, property] : m_propertyMap )
	{
		if( property.modified() )
			return true;
	}
	for( const PropertyContainer* sub : m_subContainer )
	{
		if( sub->modified() )
			return true;
	}
	return false;
}

void PropertyContainer::resetToSaved()
{
	for( auto& [name, property] : m_propertyMap )
		property.resetToSaved();
	for( PropertyContainer* sub : m_subContainer )
		sub->resetToSaved();
}
