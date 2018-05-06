#include "DMLight.h"


DMLight::DMLight( LightType _type )
{
	m_type = _type;
	m_color = D3DXVECTOR3( 1.0, 1.0, 1.0 );
	m_enabled = true;
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
	m_transformBuffer = other.m_transformBuffer;
	return *this;
}

DMLight::DMLight( const DMLight& other )
{
	*this = other;
}
/*
DMLight::DMLight( DMLight&& other )
{
	*this = std::move( other );
}

DMLight& DMLight::operator=( DMLight&& other )
{
	std::swap( m_enabled, other.m_enabled );
	std::swap( m_color, other.m_color );
	std::swap( m_type, other.m_type );
	std::swap( m_direction, other.m_direction );
	std::swap( m_spot_angle, other.m_spot_angle );
	return *this;
}*/

void DMLight::setEnabled( bool )
{

}

bool DMLight::enabled() const
{
	return m_enabled;
}

void DMLight::setColor( float r, float g, float b )
{
	m_color.x = r;
	m_color.y = g;
	m_color.z = b;
}

void DMLight::setColor( D3DXVECTOR3& _color )
{
	memcpy( &m_color, &_color, sizeof( D3DXVECTOR3 ) );
}

D3DXVECTOR3 DMLight::color( ) const
{
	return m_color;
}

void DMLight::setDirection( D3DXVECTOR3& _direction )
{
	memcpy( &m_direction, &_direction, sizeof( D3DXVECTOR3 ) );
}

void DMLight::setDirection( float x, float y, float z )
{
	m_direction.x = x;
	m_direction.y = y;
	m_direction.z = z;
}

D3DXVECTOR3 DMLight::direction() const
{
	return m_direction;
}

DMLight::LightType DMLight::type() const
{
	return m_type;
}


float DMLight::spotAngle() const
{
	return m_spot_angle;
}