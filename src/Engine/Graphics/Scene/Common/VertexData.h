#pragma once
#include "DirectX.h"
#include <string>
#include <unordered_map>
#include <type_traits>
#include "Utils\utilites.h"

namespace GS
{

class VertexData
{
public:
	VertexData();
	~VertexData();

	struct PTN
	{
		XMFLOAT3 position;
		XMFLOAT2 texture;
		XMFLOAT3 normal;
	};

	struct PTNTB
	{
		XMFLOAT3 position;
		XMFLOAT2 texture;
		XMFLOAT3 normal;
		XMFLOAT3 tangent;
		XMFLOAT3 binormal;
	};

	enum Type
	{
		V_PTN = 1, V_PTNTB = 2
	};

	static VertexData::Type type( const std::string& name );
	static const std::string& name( VertexData::Type type );

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
	VertexData::Type type( StructType* obj )
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
	static VertexData::Type type()
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

	static uint16_t size( VertexData::Type type );

	static ID3D11InputLayout* layout( VertexData::Type type );

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