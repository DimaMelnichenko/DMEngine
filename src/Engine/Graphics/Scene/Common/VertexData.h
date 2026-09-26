#pragma once
#include "DirectX.h"
#include <type_traits>
#include "Utils\utilites.h"

namespace GS
{

class VertexData
{
public:

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

	template<typename StructType>
	static VertexData::Type type()
	{
		if constexpr( std::is_same_v<StructType, PTN> )
		{
			return Type::V_PTN;
		}
		else
		{
			static_assert( std::is_same_v<StructType, PTNTB>, "Unknown vertex struct" );
			return Type::V_PTNTB;
		}
	}
};

}