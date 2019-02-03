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

size_t PropertyContainer::count() const
{
    return m_propertyMap.size();
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

PropertyContainer::PropertyMap& PropertyContainer::propertyMap()
{
	return m_propertyMap;
}