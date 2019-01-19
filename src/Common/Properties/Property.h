#pragma once

#include "value.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <cassert>
#include "Logger\Logger.h"


//------------------------------
// BasicProperty

enum class GUIControlType : int
{
	SLIDER = 1,
	COLOR = 2,
	DRAG = 3
};

class BasicProperty
{
public:
    BasicProperty( const std::string& name );
    virtual ~BasicProperty() = default;

    virtual bool typeMatched(const basic_type& var) = 0;

	virtual ValueType enumType() = 0;

    virtual const basic_type& data() const = 0;
    virtual basic_type& data() = 0;
	virtual basic_type* dataPtr() = 0;

    virtual BasicProperty* clone() const = 0;

	const std::string& name();

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
};


//------------------------------
// Property<...>

template<typename TYPE>
class Property: public BasicProperty, public type<TYPE>
{
public:
    typedef type<TYPE> value_type;

public:
	Property( const std::string& name, const TYPE& value = TYPE() ) : 
		BasicProperty( name ), 
		type<TYPE>( value )
    {
    }

    virtual Property* clone() const
    {
        return new Property(*this);
    }

    virtual bool typeMatched(const basic_type& value)
    {
        return value_type::typeMatched(value);
    }

    virtual const value_type& data() const
    {
        return *this;
    }

    virtual value_type& data()
    {
        return *this;
    }

	basic_type* dataPtr()
	{
		return this;
	}

    using value_type::setValue;

    virtual void setValue(const basic_type& value)
    {
        value_type::setValue(value);
    }
    
    operator const TYPE&() const
    {
        return value_type::value();
    }

	operator TYPE*( )
	{
		return value_type::valuePtr();
	}

    virtual void valueFromString( const std::string& s)
    {
        type<TYPE>::valueFromString(s);
    }

	ValueType enumType() override
	{
		return getValueType( valueType() );
	}
};
