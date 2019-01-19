
#include "DirectX.h"
#include "ValueType.h"
//#include "Struct.h"
//#include "List.h"
//#include "Map.h"

ValueType getValueType(const type_info& typeInfo)
{
    ValueType resVt;

    size_t typeId = typeInfo.hash_code();

	if( typeId == typeid( float ).hash_code() )
		resVt = ValueType::FLOAT;
	else if( typeId == typeid( XMFLOAT4 ).hash_code() )
		resVt = ValueType::VECTOR;
	else if( typeId == typeid( bool ).hash_code() )
		resVt = ValueType::BOOL;
	
    
	return resVt;
}

/*
ExtendedValueType getExtendedValueType(const type_info& typeInfo)
{
    ExtendedValueType result;
    size_t typeId = typeInfo.hash_code();
    if (typeId == typeid(BM::Struct).hash_code())
    	result = ExtendedValueType::Type_Struct;
    else if (typeId == typeid(BM::List).hash_code())
    	result = ExtendedValueType::Type_List;
    else if (typeId == typeid(BM::Map).hash_code())
    	result = ExtendedValueType::Type_Map;
    else
    	result = static_cast<ExtendedValueType>(getValueType(typeInfo));
    return result;
}
*/
std::string valueTypeToString(ValueType  valueType)
{
    switch (valueType)
    {
        case ValueType::FLOAT : return "float";
        case ValueType::VECTOR : return "vector";
		case ValueType::BOOL: return "bool";
        default: return "unknown";
    }
}

bool operator==( const XMFLOAT4& a, const XMFLOAT4& b )
{
	return ( a.x == b.x ) && ( a.y == b.y ) && ( a.z == b.z ) && ( a.w == b.w );
}

template<>
std::string toString( const XMFLOAT4& vec )
{
	return vec4ToStr( vec );
}

template<>
std::tuple<float, bool> fromString( const std::string& s )
{
	float fValue;
	if( auto[p, er] = std::from_chars( s.data(), s.data() + s.size(), fValue ); er != std::errc() )
	{
		return std::make_tuple( fValue, true );
	}

	return std::make_tuple( fValue, false );
}

template<>
std::tuple<XMFLOAT4, bool> fromString( const std::string& s )
{
	XMFLOAT4 vec;
	if( strToVec4( s, vec ) )
	{
		return std::make_tuple( vec, true );;
	}

	return std::make_tuple( vec, false );
}

template<>
std::tuple<bool, bool> fromString( const std::string& s )
{
	if( s == "true" || s == "1" )
		return { true,true };
	if( s == "false" || s == "0" )
		return { false,true };

	return { false,false };
}