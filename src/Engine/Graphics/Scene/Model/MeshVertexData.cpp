#include "MeshVertexData.h"

namespace GS
{

std::unordered_map<MeshVertexData::Type, std::wstring> m_idNameMap = { 
	{ MeshVertexData::Type::PTN, L"PTN" } ,
	{ MeshVertexData::Type::PTNTB, L"PTNTB" } };

MeshVertexData::Type MeshVertexData::type( const std::wstring& name )
{
	for( auto pair : m_typeNames )
	{
		if( pair.second == name )
			return pair.first;
	}

	return MeshVertexData::Type::PTN;
}

template<typename StructType>
const std::wstring& MeshVertexData::name( StructType* obj )
{
	if( std::is_same<PTN, Type>::value )
	{
		return L"PTN";
	}
	if( std::is_same<PTNTB, Type>::value )
	{
		return L"PTN";
	}
}

template<typename StructType>
MeshVertexData::Type MeshVertexData::type( StructType* obj )
{
	if( std::is_same<PTN, Type>::value )
	{
		return Type::PTN;
	}
	if( std::is_same<PTNTB, Type>::value )
	{
		return Type::PTNTB;
	}
}

const std::wstring& MeshVertexData::name( MeshVertexData::Type type )
{
	return m_typeNames[type];
}

uint16_t MeshVertexData::size( MeshVertexData::Type type )
{
	switch( type )
	{
		case Type::PTN:
			return sizeof( PTN );
			break;
		case Type::PTNTB:
			return sizeof( PTNTB );
			break;
		default:
			break;
	}

	return 0;
}

MeshVertexData::MeshVertexData()
{
}


MeshVertexData::~MeshVertexData()
{
}


}