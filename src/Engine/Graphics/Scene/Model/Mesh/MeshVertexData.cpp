#include "MeshVertexData.h"

namespace GS
{

static std::unordered_map<MeshVertexData::Type, std::wstring> m_idNameMap = { 
	{ MeshVertexData::Type::V_PTN, L"PTN" } ,
	{ MeshVertexData::Type::V_PTNTB, L"PTNTB" } };

MeshVertexData::Type MeshVertexData::type( const std::wstring& name )
{
	for( auto pair : m_typeNames )
	{
		if( pair.second == name )
			return pair.first;
	}

	return MeshVertexData::Type::V_PTN;
}



const std::wstring& MeshVertexData::name( MeshVertexData::Type type )
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