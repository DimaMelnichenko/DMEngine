#pragma once

#include <unordered_map>
#include <string>
#include <stdexcept>
#include <charconv>
#include <tuple>
#include "DirectX.h"
#include "Utils/utilites.h"

class BasicProperty;

enum class ValueType : int
{
    FLOAT = 1,
    VECTOR = 2,
	BOOL = 3
};


ValueType getValueType(const std::type_info& typeInfo);
//ExtendedValueType getExtendedValueType(const std::type_info& typeInfo);

std::string valueTypeToString(ValueType valueType);


template<typename Type> 
bool castFromStringToValueType(const std::string& srcValue, Type& destValue)
{
	float fValue;
	auto[p, ec] = std::from_chars( srcValue.data(), srcValue.data() + srcValue.size(), fValue );
	if( ec == std::errc() )
	{
		destValue = fValue;
		return true;
	}

    return true;
}


template<typename TYPE> 
bool checkValueType(TYPE val)
{
    ValueType valType = static_cast<ValueType>(val);
    
	switch( valType )
    {
        case ValueType::FLOAT:
        case ValueType::VECTOR:
		case ValueType::BOOL:
            return true;
    }
    
    return false;
}


template<typename TYPE>
ValueType convertToValueType(TYPE val)
{
    if (!checkValueType(val))
        throw std::logic_error("Unknown value type id = " + BM::toString(val));

    return static_cast<ValueType>(val);
}

template<class TYPE>
std::tuple<typename TYPE, bool> fromString( const std::string& s )
{
	return std::make_tuple<typename TYPE,bool>( TYPE(), false );
}

template<>
std::tuple<float, bool> fromString( const std::string& s );

template<>
std::tuple<XMFLOAT4, bool> fromString( const std::string& s );

template<>
std::tuple<bool, bool> fromString( const std::string& s );

template<class TYPE>
std::string toString( const TYPE& value )
{
	return std::to_string( value );
}

template<>
std::string toString( const XMFLOAT4& vec );

bool operator==( const XMFLOAT4& a, const XMFLOAT4& b );

template<class TYPE>
TYPE* readProperty( BasicProperty* prop )
{
	return dynamic_cast<Property<typename TYPE>*>( prop )->operator TYPE *();
}