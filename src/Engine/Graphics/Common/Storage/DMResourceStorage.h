#pragma once
#include "DMResource.h"
#include <unordered_map>
#include <type_traits>


class DMAbstractStorage
{
public:
	DMAbstractStorage() = default;
	virtual ~DMAbstractStorage(){}
};

template<typename ResourceType>
class DMResourceStorage : public DMAbstractStorage
{
public:
	DMResourceStorage( const std::string& path ) : m_path( path )
	{
	}
	virtual ~DMResourceStorage()
	{
	}

	ResourceType& operator[]( const std::string& name )
	{
		return m_storage[id( name )];
	}

	virtual bool load( const std::string& )
	{
		return false;
	};
	template<typename ... Args>
	bool load( uint32_t id, const std::string& name, Args ... )
	{
		return false;
	}

	const ResourceType& get( uint32_t id ) const
	{
		return m_storage[id];
	}

	ResourceType& get( uint32_t id )
	{
		return m_storage[id];
	}

	const ResourceType& get( const std::string& name )
	{
		if( !exists( name ) && !load(name) )
			return m_storage[0];

		return m_storage[id(name)];
	}

	uint32_t id( const std::string& name )
	{
		if( m_name_to_index.count( name ) )
		{
			return m_name_to_index[name];
		}
	
		return UINT32_MAX;
	}

	const std::string& path()
	{
		return m_path;
	}

	bool exists( uint32_t id )
	{
		return m_storage.count( id ) > 0;
	}

	bool exists( const std::string& name )
	{
		return m_name_to_index.count( name ) > 0;
	}

	bool clone( const std::string& name, const std::string& cloneName )
	{
		
	}

	bool insertResource( ResourceType&& resource )
	{
		if( !m_name_to_index.count( resource->name() ) )
		{
			m_name_to_index[resource->name()] = resource->id();
			m_storage.insert( std::make_pair( resource->id(), std::move( resource ) ) );
		}
		return true;
	}

	auto begin()
	{
		return m_storage.begin();
	}

	auto end()
	{
		return m_storage.end();
	}


	uint32_t nextId()
	{
		return m_id_counter++;
	}

	uint32_t size()
	{
		return m_storage.size();
	}

protected:
	std::unordered_map<uint32_t,ResourceType> m_storage;
	std::unordered_map<std::string, uint32_t> m_name_to_index;

private:
	std::string m_path;
	uint32_t m_id_counter = 0;
};

