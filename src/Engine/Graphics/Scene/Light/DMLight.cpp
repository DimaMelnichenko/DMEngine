#include "DMLight.h"


DMLight::DMLight( LightType type ) :
	m_type( type ),
	m_color( XMFLOAT3( 1.0, 1.0, 1.0 ) ),
	m_enabled( true ),
	m_attenuation( 0 )
{
	
}

DMLight::~DMLight(void)
{

}

DMLight& DMLight::operator=( const DMLight& other ) 
{
	if( this == &other )
		return *this;

	m_enabled = other.m_enabled;
	m_color = other.m_color;
	m_type = other.m_type;
	m_direction = other.m_direction;
	m_spot_angle = other.m_spot_angle;
	m_attenuation = other.m_attenuation;
	m_transformBuffer = other.m_transformBuffer;
	return *this;
}

DMLight::DMLight( const DMLight& other )
{
	*this = other;
}

bool DMLight::enabled() const
{
	return m_enabled;
}

void DMLight::setColor( XMFLOAT3& _color )
{
	memcpy( &m_color, &_color, sizeof( XMFLOAT3 ) );
}

XMFLOAT3 DMLight::color( ) const
{
	return m_color;
}

XMFLOAT3 DMLight::direction() const
{
	return m_direction;
}

DMLight::LightType DMLight::type() const
{
	return m_type;
}


DMLight::LightType DMLight::strToType( const std::string& typeName )
{
	if( typeName == "Dir" )
		return Dir;

	if( typeName == "Point" )
		return Point;

	if( typeName == "Spot" )
		return Spot;

	return Point;
}