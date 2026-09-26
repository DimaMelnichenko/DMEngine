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
	// Слот 0 занимает заглушка: её возвращают get() и operator[] вместо отсутствующего ресурса.
	// Id из base.db3 начинаются с 1, поэтому слот 0 с ними не пересекается.
	static constexpr uint32_t placeholderId = 0;

	DMResourceStorage( const std::string& path ) : m_path( path )
	{
	}
	virtual ~DMResourceStorage()
	{
	}

	ResourceType& operator[]( const std::string& name )
	{
		return get( id( name ) );
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

	// Если нет ни ресурса, ни заглушки, возвращается пустое значение; в хранилище ничего не добавляется
	const ResourceType& get( uint32_t id ) const
	{
		auto it = m_storage.find( id );
		if( it == m_storage.end() )
			it = m_storage.find( placeholderId );

		static const ResourceType empty{};
		return it != m_storage.end() ? it->second : empty;
	}

	ResourceType& get( uint32_t id )
	{
		auto it = m_storage.find( id );
		if( it == m_storage.end() )
			it = m_storage.find( placeholderId );

		static ResourceType empty{};
		return it != m_storage.end() ? it->second : empty;
	}

	const ResourceType& get( const std::string& name )
	{
		if( !exists( name ) && !load(name) )
			return get( placeholderId );

		return get( id( name ) );
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

