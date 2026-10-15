#include "PropertyContainer.h"
#include <algorithm>
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

void PropertyContainer::addButton( const std::string& name, const std::string& tooltip, std::function<ButtonAction()> run )
{
	m_buttons.push_back( { name, tooltip, std::move( run ) } );
}

const std::vector<PropertyContainer::Button>& PropertyContainer::buttons() const
{
	return m_buttons;
}

const PropertyContainer::Button* PropertyContainer::button( const std::string& name ) const
{
	for( const Button& button : m_buttons )
	{
		if( button.name == name )
			return &button;
	}
	return nullptr;
}

void PropertyContainer::setStructureModified( bool modified )
{
	m_structureModified = modified;
}

void PropertyContainer::markSaved()
{
	m_structureModified = false;
	for( auto& [name, property] : m_propertyMap )
		property.markSaved();
	for( PropertyContainer* sub : m_subContainer )
		sub->markSaved();
}

uint64_t PropertyContainer::revision() const
{
	uint64_t revision = 0;
	for( const auto& [name, property] : m_propertyMap )
		revision = std::max( revision, property.revision() );
	for( const PropertyContainer* sub : m_subContainer )
		revision = std::max( revision, sub->revision() );
	return revision;
}

bool PropertyContainer::editing() const
{
	for( const auto& [name, property] : m_propertyMap )
	{
		if( property.editing() )
			return true;
	}
	for( const PropertyContainer* sub : m_subContainer )
	{
		if( sub->editing() )
			return true;
	}
	return false;
}

bool PropertyContainer::modified() const
{
	if( m_transient )
		return false;
	if( m_structureModified )
		return true;
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
