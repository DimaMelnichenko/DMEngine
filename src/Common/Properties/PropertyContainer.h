#pragma once

#include "Property.h"
#include <unordered_map>
#include <type_traits>


class PropertyContainer
{
 private:
    using PropertyMap = std::unordered_map<std::string, BasicProperty*>;

 public:
    class const_iterator;

    class iterator
    {
        friend class const_iterator;
        friend class PropertyContainer;

    public:
        using value_type = std::remove_pointer<PropertyMap::mapped_type>::type;
        typedef value_type& reference;
        typedef value_type* pointer;
        
    public:
        iterator() = default;

        reference operator*() const { return *(m_mapIterator->second); }
        pointer operator->() const { return m_mapIterator->second; }
        
        iterator& operator++() { m_mapIterator++; return *this; }
        iterator operator++(int) { return iterator(++m_mapIterator); }    
        
        bool operator==(const iterator& iter) const { return m_mapIterator == iter.m_mapIterator; }
        bool operator==(const const_iterator& iter) const { return m_mapIterator == iter.m_mapIterator; }

        bool operator!=(const iterator& iter) const { return m_mapIterator != iter.m_mapIterator; }
        bool operator!=(const const_iterator& iter) const { return m_mapIterator != iter.m_mapIterator; }

    protected:
        iterator(const PropertyMap::iterator& map_iter) : m_mapIterator(map_iter) { }
        
    private:
        PropertyMap::iterator m_mapIterator;
    };


    class const_iterator
    {
        friend class iterator;
        friend class PropertyContainer;

    public:
        typedef std::remove_pointer<PropertyMap::mapped_type>::type value_type;
        typedef const value_type& reference;
        typedef const value_type* pointer;
        
    public:
        const_iterator() = default;
        const_iterator(const iterator& iter) : m_mapIterator(iter.m_mapIterator) { }
        
        reference operator*() const { return *(m_mapIterator->second); }
        pointer operator->() const { return m_mapIterator->second; }
        
        const_iterator& operator++() { m_mapIterator++; return *this; }
        const_iterator operator++(int) { return const_iterator(++m_mapIterator); }    
        
        bool operator==(const const_iterator& iter) const { return m_mapIterator == iter.m_mapIterator; }
        bool operator==(const iterator& iter) const { return m_mapIterator == iter.m_mapIterator; }

        bool operator!=(const const_iterator& iter) const { return m_mapIterator != iter.m_mapIterator; }
        bool operator!=(const iterator& iter) const { return m_mapIterator != iter.m_mapIterator; }

    protected:
        const_iterator(const PropertyMap::const_iterator& map_iter) : m_mapIterator(map_iter) { }
        
    private:
        PropertyMap::const_iterator m_mapIterator;
    };

public:
    PropertyContainer();
    PropertyContainer(const PropertyContainer& obj);
    PropertyContainer(PropertyContainer&& obj);

    virtual ~PropertyContainer();

    PropertyContainer& operator=(const PropertyContainer& obj);
    PropertyContainer& operator=(PropertyContainer&& obj);

    const_iterator begin() const;
    const_iterator cbegin() const;
    iterator begin();

    const_iterator end() const;
    const_iterator cend() const;
    iterator end();

    
	const BasicProperty* property( const std::string& name ) const;
	BasicProperty* property( const std::string& name );

	BasicProperty* moveProperty( BasicProperty* prop, bool replaceIfExists = false );
    void merge(PropertyContainer&& obj, bool replaceIfExists = false);
    void clearProperties();

    size_t count() const;
    
    bool exists(const std::string& name) const;
    void deleteProperty(const std::string& name);

	BasicProperty* operator[]( const std::string& name );

private:
    BasicProperty* insertProperty(const BasicProperty& src_prop, bool replaceIfExists);
    BasicProperty* insertProperty(const BasicProperty* src_prop, bool replaceIfExists);

    void assign(const PropertyContainer& obj);
    void assign(PropertyContainer&& obj);

    const BasicProperty* find_property(int kind_id) const;
    BasicProperty* find_property(int kind_id);

    const BasicProperty* find_property(const std::string& name) const;
    BasicProperty* find_property(const std::string& name);

private:
    PropertyMap m_propertyMap;
};



