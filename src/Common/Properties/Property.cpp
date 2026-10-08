
#include "Property.h"
#include <cstring>
#include <type_traits>


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

void Property::markSaved()
{
	m_saved = m_value;
	m_hasSaved = true;
}

bool Property::modified() const
{
	if( !m_hasSaved || m_saved.index() != m_value.index() )
		return false;
	// Значения — простые типы без указателей: сравнение по байтам (у XMFLOAT* нет operator==)
	return std::visit( [this]( const auto& saved )
	{
		using Type = std::decay_t<decltype( saved )>;
		return std::memcmp( &saved, &std::get<Type>( m_value ), sizeof( Type ) ) != 0;
	}, m_saved );
}

void Property::resetToSaved()
{
	if( m_hasSaved )
		m_value = m_saved;
}




