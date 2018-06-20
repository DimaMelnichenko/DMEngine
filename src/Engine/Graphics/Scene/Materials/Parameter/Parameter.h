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
	Parameter( const XMFLOAT3& vec ) : m_vec4( vec.x, vec.y, vec.z, 1.0 )
	{
	}
	Parameter( const XMFLOAT4& vec ) : m_vec4( vec )
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

	const XMFLOAT4& vector() const
	{
		return m_vec4;
	}
	
private:
	uint32_t m_textId;
	float m_scalar;
	XMFLOAT4 m_vec4;
};

using ParamSet = std::unordered_map<std::string, Parameter>;

}
