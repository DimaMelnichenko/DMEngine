#pragma once
#include "DirectX.h"
#include <stdint.h>
#include <unordered_map>

namespace GS
{

class Parameter
{
public:
	enum ValueType
	{
		textureId, float4, scalarValue, none
	};
public:
	Parameter() : m_valueType( none )
	{
	}
	Parameter( uint32_t id ) : m_textId(id), m_valueType(textureId)
	{
	}
	Parameter( const XMFLOAT3& vec ) : m_vec4( vec.x, vec.y, vec.z, 1.0 ), m_valueType( float4 )
	{
	}
	Parameter( const XMFLOAT4& vec ) : m_vec4( vec ), m_valueType( float4 )
	{
	}
	Parameter( float value ) : m_scalar(value), m_valueType( scalarValue )
	{
	}
	~Parameter()
	{
	}

	uint32_t textId() const
	{
		return m_textId;
	}

	uint32_t& textId()
	{
		return m_textId;
	}

	uint32_t* textIdPtr()
	{
		return &m_textId;
	}

	void setValue( uint32_t value )
	{
		m_textId = value;
	}

	float scalar() const
	{
		return m_scalar;
	}

	const XMFLOAT4& vector() const
	{
		return m_vec4;
	}

	XMFLOAT4& vector()
	{
		return m_vec4;
	}

	XMFLOAT4* vectorPtr()
	{
		return &m_vec4;
	}

	void setValue( const XMFLOAT4& value )
	{
		m_vec4 = value;
	}
	
	ValueType valueType() const
	{
		return m_valueType;
	}

	uint32_t m_id;

private:
	uint32_t m_textId;
	float m_scalar;
	XMFLOAT4 m_vec4;
	ValueType m_valueType;
	
};

using ParamSet = std::unordered_map<std::string, Parameter>;

}
