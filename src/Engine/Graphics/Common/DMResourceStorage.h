#pragma once
#include "DMResource.h"
#include <unordered_map>

class DMAbstrctStorage
{
public:
	DMAbstrctStorage();
	virtual ~DMAbstrctStorage();
};

template<class ResourceType>
class DMResourceStorage : public DMAbstrctStorage
{
public:
	DMResourceStorage( const std::wstring& path ) : m_path( path )
	{
	}
	virtual ~DMResourceStorage()
	{
	}

	virtual bool load( const std::wstring& ) = 0;

	const ResourceType& get( uint32_t id )
	{
		m_storage[id];
	}

	uint32_t id( const std::wstring& name )
	{
		if( m_name_to_index.count( name ) )
		{
			return m_name_to_index[name];
		}

		return UINT32_MAX;
	}

	const std::wstring& path()
	{
		return m_path;
	}

protected:
	bool insertResource( const std::wstring& name, ResourceType&& resource )
	{
		if( !m_name_to_index.count( name ) )
		{
			m_name_to_index[name] = m_id_counter;
			m_storage.insert( std::make_pair( (uint32_t)m_id_counter, std::move(resource) ) );
		}

		return true;
	}

	uint32_t nextId()
	{
		return m_id_counter;
	}

protected:
	std::unordered_map<uint32_t,ResourceType> m_storage;
	std::unordered_map<std::wstring, uint32_t> m_name_to_index;

private:
	std::wstring m_path;
	uint32_t m_id_counter;
};

