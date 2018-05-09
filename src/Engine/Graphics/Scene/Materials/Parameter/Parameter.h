#pragma once
#include "DirectX.h"
#include <stdint.h>
#include <unordered_map>

namespace GS
{

class Parameter
{
public:
	Parameter( uint32_t id ) : m_textId(id)
	{
	}
	Parameter( const D3DXVECTOR3& vec3 ) : m_vec3( vec3 )
	{
	}
	Parameter( float scalar ) : m_scalar(scalar)
	{
	}
	~Parameter()
	{
	}

	uint32_t textId() const
	{
		return m_textId;
	}

	float scalar() const
	{
		return m_scalar;
	}

	const D3DXVECTOR3& vector() const
	{
		return m_vec3;
	}
	
private:
	uint32_t m_textId;
	float m_scalar;
	D3DXVECTOR3 m_vec3;
};

using ParamSet = std::unordered_map<std::string, Parameter>;

}
