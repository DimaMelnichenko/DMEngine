
#include "Property.h"
#include <cstring>
#include <type_traits>


//--------------------------------------------------
// Property

Property::Property() : m_revision( ++s_revisionClock )
{

}

bool Property::sameValue( const Value& a, const Value& b )
{
	if( a.index() != b.index() )
		return false;
	return std::visit( [&b]( const auto& value )
	{
		using Type = std::decay_t<decltype( value )>;
		return std::memcmp( &value, &std::get<Type>( b ), sizeof( Type ) ) == 0;
	}, a );
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

void Property::markSaved()
{
	m_saved = m_value;
	m_hasSaved = true;
}

bool Property::modified() const
{
	if( m_transient || !m_hasSaved || m_saved.index() != m_value.index() )
		return false;
	return !sameValue( m_saved, m_value );
}

void Property::resetToSaved()
{
	if( m_hasSaved )
		setValue( m_saved );
}




