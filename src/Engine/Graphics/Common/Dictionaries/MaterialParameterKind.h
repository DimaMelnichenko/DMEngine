#pragma once
#include <stdint.h>
#include <string>
#include <unordered_map>
#include "ParameterType.h"


class MaterialParameterKind
{
public:
	struct ItemType;
	using DictionaryMap = typename std::unordered_map<uint32_t, ItemType>;
public:
	MaterialParameterKind();
	virtual ~MaterialParameterKind();

	bool load();

	const ItemType& get( uint32_t id ) const;
	const ItemType* get( const std::string& name ) const;

	uint32_t add( const std::string& name, ParameterType type );

	DictionaryMap dictionary()	
	{ 
		return m_dictionary;
	}
	const DictionaryMap& dictionary() const
	{
		return m_dictionary;
	}
	
public:
	struct ItemType
	{
		uint32_t id;
		std::string name;
		ParameterType type;
	};

private:
	bool insertRow( const ItemType& );

private:
	DictionaryMap m_dictionary;
	uint32_t m_lastId = 0;
};

