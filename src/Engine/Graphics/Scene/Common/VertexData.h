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

	// Данные ветра дерева на вершину (второй поток VertexPool, слот 1) — как входы Games wind SpeedTree: начало ветви
	// первого и второго уровня и вес (0 у начала → 1 к кончику), доля высоты дерева и вес ряби листвы. Координаты —
	// меша. У мешей без данных ветра — нули. Раскладка — входы WIND_TREE в Shaders/LightShader.vs
	struct Wind
	{
		XMFLOAT4 branch1;	// xyz — начало ветви на стволе (in_wind_branch1_origin), w — вес (in_wind_branch1_weight)
		XMFLOAT4 branch2;	// то же для веточки (branch2)
		XMFLOAT2 weights;	// x — доля высоты дерева (общее качание), y — вес ряби (in_wind_ripple)
	};
	static_assert( sizeof( Wind ) == 40, "VertexData::Wind layout: file block WIND" );

	// То же во втором потоке VertexPool — половинная точность (DXGI_FORMAT_R16*_FLOAT): чтение 40 байт на вершину стоило
	// деревьям ~0,2 мс в тенях и depth prepass. Начала ветвей в пределах ±30 м — ~1,5 см, веса — ~0,0005
	struct WindHalf
	{
		uint16_t branch1[4];
		uint16_t branch2[4];
		uint16_t weights[2];
	};
	static_assert( sizeof( WindHalf ) == 20, "VertexPool wind stream layout (input slot 1)" );

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