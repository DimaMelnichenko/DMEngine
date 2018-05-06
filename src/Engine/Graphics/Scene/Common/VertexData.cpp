#include "VertexData.h"

namespace GS
{


std::unordered_map<VertexData::Type, std::string> VertexData::m_typeNames = {
	{ VertexData::Type::V_PTN, "PTN" } ,
	{ VertexData::Type::V_PTNTB, "PTNTB" } };

VertexData::Type VertexData::type( const std::string& name )
{
	for( auto pair : m_typeNames )
	{
		if( pair.second == name )
			return pair.first;
	}

	return VertexData::Type::V_PTN;
}



const std::string& VertexData::name( VertexData::Type type )
{
	return m_typeNames[type];
}

uint16_t VertexData::size( VertexData::Type type )
{
	switch( type )
	{
		case Type::V_PTN:
			return sizeof( PTN );
			break;
		case Type::V_PTNTB:
			return sizeof( PTNTB );
			break;
		default:
			break;
	}

	return 0;
}


VertexData::VertexData()
{
}


VertexData::~VertexData()
{
}


}