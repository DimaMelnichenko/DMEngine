#pragma once
#include "DMResource.h"
#include <unordered_map>

class DMAbstrctStorage
{
public:
	DMAbstrctStorage() = default;
	virtual ~DMAbstrctStorage(){}
};

template<typename ResourceType>
class DMResourceStorage : public DMAbstrctStorage
{
public:
	DMResourceStorage( const std::string& path ) : m_path( path )
	{
	}
	virtual ~DMResourceStorage()
	{
	}

	virtual bool load( const std::string& ) = 0;

	const ResourceType& get( uint32_t id )
	{
		return m_storage[id];
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
		return m_storage.count( id );
	}

	bool exists( const std::string& name )
	{
		return m_name_to_index.count( name );
	}

	bool insertResource( const std::string& name, ResourceType&& resource )
	{
		if( !m_name_to_index.count( name ) )
		{
			m_name_to_index[name] = m_id_counter;
			m_storage.insert( std::make_pair( m_id_counter, std::move(resource) ) );
			m_id_counter++;
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

protected:
	uint32_t nextId()
	{
		return m_id_counter;
	}

protected:
	std::unordered_map<uint32_t,ResourceType> m_storage;
	std::unordered_map<std::string, uint32_t> m_name_to_index;

private:
	std::string m_path;
	uint32_t m_id_counter = 0;
};

