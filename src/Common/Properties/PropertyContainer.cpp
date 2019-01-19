#include "PropertyContainer.h"
#include <functional>


//--------------------------------------------
// PropertyContainer

PropertyContainer::PropertyContainer()
{
}


PropertyContainer::PropertyContainer(const PropertyContainer& obj)
{
    assign(obj);
}


PropertyContainer::PropertyContainer(PropertyContainer&& obj)
{
    assign(std::move(obj));
}


PropertyContainer::~PropertyContainer()
{
    clearProperties();
}


PropertyContainer& PropertyContainer::operator=(const PropertyContainer& obj)
{
    assign(obj);

    return *this;
}


PropertyContainer& PropertyContainer::operator=(PropertyContainer&& obj)
{
    assign(std::move(obj));

    return *this;
}


void PropertyContainer::assign(const PropertyContainer& obj)
{
	if( this != &obj )
	{
		clearProperties();
		for( auto& prop : obj )
			insertProperty( prop, false );
	}
}

void PropertyContainer::assign(PropertyContainer&& obj)
{
    if( this != &obj ) 
	{
        clearProperties();

        m_propertyMap = std::move(obj.m_propertyMap);
    }
}


PropertyContainer::const_iterator PropertyContainer::begin() const
{
    return const_iterator(m_propertyMap.begin());
}


PropertyContainer::const_iterator PropertyContainer::cbegin() const
{
    return const_iterator(m_propertyMap.begin());
}


PropertyContainer::const_iterator PropertyContainer::end() const
{
    return const_iterator(m_propertyMap.end());
}


PropertyContainer::const_iterator PropertyContainer::cend() const
{
    return const_iterator(m_propertyMap.end());
}


PropertyContainer::iterator PropertyContainer::begin()
{
    return iterator(m_propertyMap.begin());
}


PropertyContainer::iterator PropertyContainer::end()
{
    return iterator(m_propertyMap.end());
}


BasicProperty* PropertyContainer::insertProperty(const BasicProperty& src_prop, bool replaceIfExists)
{
    BasicProperty* newProperty = nullptr;

    newProperty = src_prop.clone();
    
	if( auto [iter,res] = m_propertyMap.insert( { newProperty->name(), newProperty } ); !res )
	{
		if( replaceIfExists )
		{
            BasicProperty*& curr_prop = iter->second;
            
            delete curr_prop; 
            curr_prop = newProperty;
        }
        else 
		{
            delete newProperty;
			newProperty = nullptr;
        }
    }
    
    return newProperty;
}


BasicProperty* PropertyContainer::insertProperty(const BasicProperty* srcProp, bool replaceIfExists)
{
	if( srcProp == nullptr )
		return nullptr;

    return insertProperty(*srcProp, replaceIfExists);
}


BasicProperty* PropertyContainer::moveProperty(BasicProperty* srcProp, bool replaceIfExists)
{
	if( srcProp != nullptr )
	{
		if( auto[iter, res] = m_propertyMap.insert( { srcProp->name(), srcProp } ); !res )
		{
			if( replaceIfExists )
			{
				BasicProperty*& curr_prop = iter->second;

				delete curr_prop;
				curr_prop = srcProp;
			}
		}
	}
	return srcProp;
}

void PropertyContainer::merge(PropertyContainer&& source, bool replaceIfExists )
{
    for( auto& [name, prop] : source.m_propertyMap )
    {
        moveProperty( prop, replaceIfExists);
        prop = nullptr;
    }
    source.clearProperties();
}

void PropertyContainer::clearProperties()
{
    for( auto [key,value] : m_propertyMap )
        delete value;
    
    m_propertyMap.clear();
}

const BasicProperty* PropertyContainer::find_property(const std::string& name) const
{
	if( m_propertyMap.count( name ) )
		return m_propertyMap.at( name );

	return nullptr;
}

BasicProperty* PropertyContainer::find_property(const std::string& name)
{
	if( m_propertyMap.count( name ) )
		return m_propertyMap.at( name );

	return nullptr;
}

const BasicProperty* PropertyContainer::property(const std::string& name) const
{
    return find_property(name);
}

BasicProperty* PropertyContainer::property(const std::string& name)
{
    return find_property(name);
}

size_t PropertyContainer::count() const
{
    return m_propertyMap.size();
}

bool PropertyContainer::exists(const std::string& name) const
{
    return find_property(name) != NULL;
}

void PropertyContainer::deleteProperty(const std::string& name)
{
    PropertyMap::const_iterator prop_iter = m_propertyMap.find(name);
    if( prop_iter != m_propertyMap.end() )
	{
        delete prop_iter->second;
        m_propertyMap.erase(prop_iter);
    }
}

BasicProperty* PropertyContainer::operator[]( const std::string& name )
{
	return find_property( name );
}


