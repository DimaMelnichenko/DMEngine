#include "MeshVertexData.h"

namespace GS
{

std::unordered_map<MeshVertexData::Type, std::string> MeshVertexData::m_typeNames = {
	{ MeshVertexData::Type::V_PTN, "PTN" } ,
	{ MeshVertexData::Type::V_PTNTB, "PTNTB" } };

MeshVertexData::Type MeshVertexData::type( const std::string& name )
{
	for( auto pair : m_typeNames )
	{
		if( pair.second == name )
			return pair.first;
	}

	return MeshVertexData::Type::V_PTN;
}



const std::string& MeshVertexData::name( MeshVertexData::Type type )
{
	return m_typeNames[type];
}

uint16_t MeshVertexData::size( MeshVertexData::Type type )
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

MeshVertexData::MeshVertexData()
{
}


MeshVertexData::~MeshVertexData()
{
}


}