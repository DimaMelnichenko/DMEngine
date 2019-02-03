#pragma once

#include "DirectX.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <cassert>
#include <variant>
#include "Logger\Logger.h"


//------------------------------
// BasicProperty

enum class GUIControlType : int
{
	SLIDER = 1,
	COLOR = 2,
	DRAG = 3,
	TEXTURE = 4
};

enum class ValueType : int
{
	BOOL = 0,
	FLOAT = 1,
	VECTOR2 = 2,
	VECTOR3 = 3,
	VECTOR4 = 4,
	INT = 5,
	UINT = 6,
};

//ValueType convertStrinTypeToType

class Property
{
private:
	using Container = std::variant<bool, float, XMFLOAT2, XMFLOAT3, XMFLOAT4, int32_t, uint32_t>;
public:
	Property();
    Property( const std::string& name );

	template<class TYPE>
	Property( const std::string& name, const TYPE& value ) : m_name(name), m_value(value)
	{

	}
    virtual ~Property() = default;

	ValueType valueType();

	template<class TYPE>
	const TYPE& data() const
	{
		return std::get<TYPE>( m_value );
	}

	template<class TYPE>
	TYPE* dataPtr()
	{
		return std::get_if<TYPE>( &m_value );
	}

	template<class TYPE>
	void setData( const TYPE& value )
	{
		m_value = value;
	}

	const std::string& name() const;
	float low() const;
	float high() const;
	GUIControlType controlType();

	void setLow( float );
	void setHigh( float );
	void setControlType( GUIControlType type );
private:
	std::string m_name;
	float m_lowBorder = 0.0;
	float m_highBorder = 1.0;
	GUIControlType m_controlType;
	Container m_value;
	ValueType m_valueType;
};

