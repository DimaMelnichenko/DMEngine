#include "DMLight.h"
#include <algorithm>

using namespace DirectX;


const SettingsSchema<DMLight::ShadowSettings>& DMLight::shadowSchema()
{
	using Settings = ShadowSettings;
	static const SettingsSchema<Settings> schema = {
		{ "cast_shadows", "Cast shadows", DM_SETTING( castShadows ) },
		{ "dynamic_shadow_distance", "Dynamic shadow distance", DM_SETTING( dynamicShadowDistance ), 20.0f, 1000.0f, "m" },
		{ "cascade_distribution_exponent", "Cascade distribution exponent", DM_SETTING( cascadeDistributionExponent ), 1.0f, 6.0f },
		{ "cascade_transition_fraction", "Cascade transition fraction", DM_SETTING( cascadeTransitionFraction ), 0.0f, 0.5f },
		{ "shadow_distance_fadeout_fraction", "Shadow distance fadeout fraction", DM_SETTING( shadowDistanceFadeoutFraction ), 0.0f, 0.5f },
		{ "shadow_bias", "Shadow bias", DM_SETTING( shadowBias ), 0.0f, 5.0f, "texels" },
		{ "normal_bias", "Normal bias", DM_SETTING( normalBias ), 0.0f, 5.0f, "texels" },
		{ "shadow_slope_bias", "Shadow slope bias", DM_SETTING( shadowSlopeBias ), 0.0f, 8.0f },
		{ "far_shadows", "Far shadows", DM_SETTING( farShadows ), 0.0f, 0.0f, "",
		  "Shadows beyond the dynamic shadow distance: the forest - by a far cascade over the whole map, the terrain - by a map of the "
		  "shadow edge height (Far Shadow Cascades in UE)" },
		{ "far_shadow_penumbra", "Far shadow penumbra", DM_SETTING( farShadowPenumbra ), 0.1f, 5.0f, "deg" },
	};
	return schema;
}

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
