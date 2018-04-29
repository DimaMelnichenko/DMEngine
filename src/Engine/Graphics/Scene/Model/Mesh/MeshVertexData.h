#pragma once
#include "DirectX.h"
#include <string>
#include <unordered_map>
#include <type_traits>
#include "Utils\utilites.h"

namespace GS
{

class MeshVertexData
{
public:
	MeshVertexData();
	~MeshVertexData();

	struct PTN
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
		D3DXVECTOR3 normal;
	};

	struct PTNTB
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
		D3DXVECTOR3 normal;
		D3DXVECTOR3 tangent;
		D3DXVECTOR3 binormal;
	};

	enum Type
	{
		V_PTN = 1, V_PTNTB = 2
	};

	static MeshVertexData::Type type( const std::string& name );
	static const std::string& name( MeshVertexData::Type type );

	template<typename StructType>
	const std::string& name( StructType* obj )
	{
		if( std::is_same<PTN, Type>::value )
		{
			return "PTN";
		}
		if( std::is_same<PTNTB, Type>::value )
		{
			return "PTN";
		}
	}

	template<typename StructType>
	MeshVertexData::Type type( StructType* obj )
	{
		if( std::is_same<PTN, Type>::value )
		{
			return Type::V_PTN;
		}
		if( std::is_same<PTNTB, Type>::value )
		{
			return Type::V_PTNTB;
		}
	}

	template<typename StructType>
	static MeshVertexData::Type type()
	{
		if( std::is_same<PTN, Type>::value )
		{
			return Type::V_PTN;
		}
		if( std::is_same<PTNTB, Type>::value )
		{
			return Type::V_PTNTB;
		}
	}

	static uint16_t size( MeshVertexData::Type type );

	static ID3D11InputLayout* layout( MeshVertexData::Type type );

private:
	/*struct EnumClassHash
	{
		template <typename T>
		std::size_t operator()( T t ) const
		{
			return static_cast<std::size_t>( t );
		}
	};*/
	static std::unordered_map<Type,std::string> m_typeNames;

};

}