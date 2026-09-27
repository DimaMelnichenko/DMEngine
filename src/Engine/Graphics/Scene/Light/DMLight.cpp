#include "DMLight.h"
#include <algorithm>


DMLight::DMLight( LightType type ) :
	m_type( type )
{
}

bool DMLight::enabled() const
{
	return m_enabled;
}

void DMLight::setEnabled( bool enabled )
{
	m_enabled = enabled;
}

DMLight::LightType DMLight::type() const
{
	return m_type;
}

void DMLight::setColor( const XMFLOAT3& color )
{
	m_color = color;
}

XMFLOAT3 DMLight::color( ) const
{
	return m_color;
}

void DMLight::setIntensity( float intensity )
{
	m_intensity = std::max( intensity, 0.0f );
}

float DMLight::intensity() const
{
	return m_intensity;
}

XMFLOAT3 DMLight::radiance() const
{
	return XMFLOAT3( m_color.x * m_intensity, m_color.y * m_intensity, m_color.z * m_intensity );
}

void DMLight::setPosition( const XMFLOAT3& position )
{
	m_position = position;
}

const XMFLOAT3& DMLight::position() const
{
	return m_position;
}

void DMLight::setDirection( const XMFLOAT3& direction )
{
	XMVECTOR vector = XMLoadFloat3( &direction );
	if( XMVectorGetX( XMVector3LengthSq( vector ) ) > 1e-12f )
		XMStoreFloat3( &m_direction, XMVector3Normalize( vector ) );
}

XMFLOAT3 DMLight::direction() const
{
	return m_direction;
}

void DMLight::setAttenuationRadius( float radius )
{
	m_attenuationRadius = std::max( radius, 0.0f );
}

float DMLight::attenuationRadius() const
{
	return m_attenuationRadius;
}

void DMLight::setConeAngles( float inner, float outer )
{
	// Шире 89° конус вырождается в полусферу; внутренний не шире внешнего
	m_outerConeAngle = std::clamp( outer, 0.0f, 89.0f );
	m_innerConeAngle = std::clamp( inner, 0.0f, m_outerConeAngle );
}

float DMLight::innerConeAngle() const
{
	return m_innerConeAngle;
}

float DMLight::outerConeAngle() const
{
	return m_outerConeAngle;
}

const DMLight::ShadowSettings& DMLight::shadowSettings() const
{
	return m_shadowSettings;
}

void DMLight::setShadowSettings( const ShadowSettings& settings )
{
	m_shadowSettings = settings;
}

DMLight::LightType DMLight::strToType( const std::string& typeName )
{
	if( typeName == "directional" )
		return Dir;

	if( typeName == "spot" )
		return Spot;

	return Point;
}

const char* DMLight::typeName( LightType type )
{
	switch( type )
	{
		case Dir: return "directional";
		case Spot: return "spot";
		default: return "point";
	}
}

bool DMLight::atmosphereSunLight() const
{
	return m_atmosphereSunLight;
}

void DMLight::setAtmosphereSunLight( bool atmosphereSunLight )
{
	m_atmosphereSunLight = atmosphereSunLight;
}
