
#include "Property.h"


//--------------------------------------------------
// BasicProperty

BasicProperty::BasicProperty( const std::string& name ) : m_name( name )
{
}

const std::string& BasicProperty::name()
{
	return m_name;
}

float BasicProperty::low() const
{
	return m_lowBorder;
}

float BasicProperty::high() const
{
	return m_highBorder;
}

void BasicProperty::setLow( float val )
{
	m_lowBorder = val;
}

void BasicProperty::setHigh( float val )
{
	m_highBorder = val;
}

GUIControlType BasicProperty::controlType()
{
	return m_controlType;
}

void BasicProperty::setControlType( GUIControlType type )
{
	m_controlType = type;
}




