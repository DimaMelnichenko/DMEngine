
#include "Property.h"


//--------------------------------------------------
// Property

Property::Property()
{

}

ValueType Property::valueType()
{
	return static_cast<ValueType>( m_value.index() );
}

float Property::low() const
{
	return m_lowBorder;
}

float Property::high() const
{
	return m_highBorder;
}

void Property::setLow( float val )
{
	m_lowBorder = val;
}

void Property::setHigh( float val )
{
	m_highBorder = val;
}

GUIControlType Property::controlType()
{
	return m_controlType;
}

void Property::setControlType( GUIControlType type )
{
	m_controlType = type;
}




