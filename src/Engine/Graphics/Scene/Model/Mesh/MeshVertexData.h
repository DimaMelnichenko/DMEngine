#pragma once
#include "DirectX.h"
#include <string>
#include <unordered_map>
#include <type_traits>

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

	static MeshVertexData::Type type( const std::wstring& name );
	static const std::wstring& name( MeshVertexData::Type type );

	template<typename StructType>
	static const std::wstring& name( StructType* obj );
	template<typename StructType>
	static MeshVertexData::Type type( StructType* obj );
	template<typename StructType>
	static MeshVertexData::Type type();

	static uint16_t size( MeshVertexData::Type type );

private:
	static std::unordered_map<Type,std::wstring> m_typeNames;
};

}