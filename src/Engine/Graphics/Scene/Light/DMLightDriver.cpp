#include "DMLightDriver.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Logger\Logger.h"

namespace
{

// Направление по углам, как Rotation у источника в UE: Pitch < 0 — свет идёт вниз, Yaw — поворот вокруг вертикали
// от +Z к +X, градусы
XMFLOAT3 directionFromRotation( const XMFLOAT2& rotation )
{
	const float pitch = XMConvertToRadians( rotation.x );
	const float yaw = XMConvertToRadians( rotation.y );
	return XMFLOAT3( std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ), std::cos( pitch ) * std::cos( yaw ) );
}

XMFLOAT2 rotationFromDirection( const XMFLOAT3& direction )
{
	return XMFLOAT2( XMConvertToDegrees( std::asin( std::clamp( direction.y, -1.0f, 1.0f ) ) ),
					 XMConvertToDegrees( std::atan2( direction.x, direction.z ) ) );
}

template<class TYPE>
void addControl( PropertyContainer& properties, const char* name, const TYPE& value, GUIControlType control, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setControlType( control );
	property->setLow( low );
	property->setHigh( high );
}

}

DMLightDriver::DMLightDriver()
{
	m_properties.setName( "Lights" );
}


DMLightDriver::~DMLightDriver(void)
{
}

bool DMLightDriver::Initialize()
{
	m_structBuffer.createBuffer( sizeof( LightBuffer ), maxLights );
	m_lightParamBuffer.reserve( maxLights );

	return true;
}

void DMLightDriver::load( LightList lights )
{
	// Устойчивая сортировка: направленные первыми в порядке базы, солнце — первый включённый из них
	std::stable_sort( lights.begin(), lights.end(), []( const DMLight& a, const DMLight& b )
	{
		return (int)a.type() < (int)b.type();
	} );
	m_light_list = std::move( lights );

	m_controls.clear();
	m_properties.subContainer().clear();
	for( uint32_t i = 0; i < m_light_list.size(); ++i )
		createProperties( m_light_list[i], i );

	if( m_light_list.size() > maxLights )
		LOG( "Level has " + std::to_string( m_light_list.size() ) + " lights, only " + std::to_string( maxLights ) + " enabled ones are used" );
	m_bufferChanged = true;
}

void DMLightDriver::createProperties( const DMLight& light, uint32_t index )
{
	LightControls controls;
	const std::string name = light.name.empty() ? DMLight::typeName( light.type() ) : light.name;
	controls.properties = std::make_unique<PropertyContainer>( std::to_string( index ) + ": " + name + " (" +
															   DMLight::typeName( light.type() ) + ")" );
	PropertyContainer& properties = *controls.properties;

	properties.insert( "Enabled", light.enabled() );
	addControl( properties, "Color", light.color(), GUIControlType::COLOR, 0.0f, 1.0f );
	// Направленный — люксы (полуденное солнце ~100 000), точечный и прожектор — канделы (лампа 100 Вт ~130)
	addControl( properties, light.type() == DMLight::Dir ? "Intensity (lx)" : "Intensity (cd)", light.intensity(),
				GUIControlType::SLIDER, 0.0f, light.type() == DMLight::Dir ? 150000.0f : 20000.0f );

	controls.rotation = rotationFromDirection( light.direction() );
	if( light.type() != DMLight::Point )
	{
		addControl( properties, "Pitch", controls.rotation.x, GUIControlType::SLIDER, -90.0f, 90.0f );
		addControl( properties, "Yaw", controls.rotation.y, GUIControlType::SLIDER, -180.0f, 180.0f );
	}
	if( light.type() != DMLight::Dir )
	{
		// Границы 0, 0 — без ограничения
		addControl( properties, "Position", light.position(), GUIControlType::DRAG, 0.0f, 0.0f );
		addControl( properties, "Attenuation radius", light.attenuationRadius(), GUIControlType::SLIDER, 0.0f, 500.0f );
	}
	if( light.type() == DMLight::Spot )
	{
		addControl( properties, "Inner cone angle", light.innerConeAngle(), GUIControlType::SLIDER, 0.0f, 89.0f );
		addControl( properties, "Outer cone angle", light.outerConeAngle(), GUIControlType::SLIDER, 0.0f, 89.0f );
	}
	if( light.type() == DMLight::Dir )
	{
		const DMLight::ShadowSettings& shadows = light.shadowSettings();
		properties.insert( "Cast shadows", shadows.castShadows );
		addControl( properties, "Dynamic shadow distance", shadows.dynamicShadowDistance, GUIControlType::SLIDER, 20.0f, 1000.0f );
		addControl( properties, "Cascade distribution exponent", shadows.cascadeDistributionExponent, GUIControlType::SLIDER, 1.0f, 6.0f );
		addControl( properties, "Cascade transition fraction", shadows.cascadeTransitionFraction, GUIControlType::SLIDER, 0.0f, 0.5f );
		addControl( properties, "Shadow distance fadeout fraction", shadows.shadowDistanceFadeoutFraction, GUIControlType::SLIDER, 0.0f, 0.5f );
		addControl( properties, "Shadow bias", shadows.shadowBias, GUIControlType::SLIDER, 0.0f, 5.0f );
		addControl( properties, "Normal bias", shadows.normalBias, GUIControlType::SLIDER, 0.0f, 5.0f );
		addControl( properties, "Shadow slope bias", shadows.shadowSlopeBias, GUIControlType::SLIDER, 0.0f, 8.0f );
	}

	m_properties.addSubContainer( controls.properties.get() );
	m_controls.push_back( std::move( controls ) );
}

void DMLightDriver::update()
{
	for( size_t i = 0; i < m_light_list.size(); ++i )
	{
		DMLight& light = m_light_list[i];
		LightControls& controls = m_controls[i];
		PropertyContainer& properties = *controls.properties;

		light.setEnabled( properties["Enabled"].data<bool>() );
		light.setColor( properties["Color"].data<XMFLOAT3>() );
		light.setIntensity( properties[light.type() == DMLight::Dir ? "Intensity (lx)" : "Intensity (cd)"].data<float>() );
		if( light.type() != DMLight::Point )
		{
			// Направление пересчитывается из углов, только когда их сдвинули: иначе оно осталось бы как в базе
			const XMFLOAT2 rotation( properties["Pitch"].data<float>(), properties["Yaw"].data<float>() );
			if( rotation.x != controls.rotation.x || rotation.y != controls.rotation.y )
			{
				light.setDirection( directionFromRotation( rotation ) );
				controls.rotation = rotation;
			}
		}
		if( light.type() != DMLight::Dir )
		{
			light.setPosition( properties["Position"].data<XMFLOAT3>() );
			light.setAttenuationRadius( properties["Attenuation radius"].data<float>() );
		}
		if( light.type() == DMLight::Spot )
			light.setConeAngles( properties["Inner cone angle"].data<float>(), properties["Outer cone angle"].data<float>() );
		if( light.type() == DMLight::Dir )
		{
			DMLight::ShadowSettings shadows;
			shadows.castShadows = properties["Cast shadows"].data<bool>();
			shadows.dynamicShadowDistance = properties["Dynamic shadow distance"].data<float>();
			shadows.cascadeDistributionExponent = properties["Cascade distribution exponent"].data<float>();
			shadows.cascadeTransitionFraction = properties["Cascade transition fraction"].data<float>();
			shadows.shadowDistanceFadeoutFraction = properties["Shadow distance fadeout fraction"].data<float>();
			shadows.shadowBias = properties["Shadow bias"].data<float>();
			shadows.normalBias = properties["Normal bias"].data<float>();
			shadows.shadowSlopeBias = properties["Shadow slope bias"].data<float>();
			light.setShadowSettings( shadows );
		}
	}

	std::vector<LightBuffer> buffer;
	buffer.reserve( maxLights );
	for( const DMLight& light : m_light_list )
	{
		if( !light.enabled() || buffer.size() == maxLights )
			continue;

		LightBuffer lightBuffer = {};
		lightBuffer.position = light.position();
		lightBuffer.type = (int)light.type();
		lightBuffer.direction = light.direction();
		lightBuffer.attenuationRadius = light.attenuationRadius();
		lightBuffer.color = light.radiance();
		lightBuffer.cosOuterCone = cosf( XMConvertToRadians( light.outerConeAngle() ) );
		lightBuffer.cosInnerCone = cosf( XMConvertToRadians( light.innerConeAngle() ) );
		buffer.push_back( lightBuffer );
	}
	m_sunIndex = !buffer.empty() && buffer[0].type == DMLight::Dir ? 0 : -1;

	if( buffer.empty() )
	{
		LightBuffer lightBuffer = {};
		lightBuffer.type = DMLight::Dir;
		lightBuffer.direction = fallbackDirection();
		lightBuffer.color = XMFLOAT3( fallbackIlluminance, fallbackIlluminance, fallbackIlluminance );
		buffer.push_back( lightBuffer );
	}

	// Источники меняются редко (правка в GUI): буфер на GPU обновляется, только если он другой
	if( buffer.size() != m_lightParamBuffer.size() ||
		std::memcmp( buffer.data(), m_lightParamBuffer.data(), buffer.size() * sizeof( LightBuffer ) ) != 0 )
	{
		m_lightParamBuffer = std::move( buffer );
		m_bufferChanged = true;
	}
}

uint32_t DMLightDriver::setBuffer( int8_t slot, SRVType type )
{
	if( m_bufferChanged && !m_lightParamBuffer.empty() )
	{
		m_structBuffer.updateData( m_lightParamBuffer.data(), m_lightParamBuffer.size() * sizeof( LightBuffer ) );
		m_bufferChanged = false;
	}

	m_structBuffer.setToSlot( slot, type );

	return static_cast<uint32_t>( m_lightParamBuffer.size() );
}

const DMLight* DMLightDriver::sun() const
{
	for( const auto& light : m_light_list )
	{
		if( light.enabled() )
			return light.type() == DMLight::Dir ? &light : nullptr;
	}
	return nullptr;
}

void DMLightDriver::directionalLight( XMFLOAT3& direction, XMFLOAT3& color ) const
{
	XMFLOAT3 lightDirection = fallbackDirection();
	color = XMFLOAT3( fallbackIlluminance, fallbackIlluminance, fallbackIlluminance );
	if( const DMLight* light = sun() )
	{
		lightDirection = light->direction();
		color = light->radiance();
	}
	else if( std::any_of( m_light_list.begin(), m_light_list.end(), []( const DMLight& light ) { return light.enabled(); } ) )
	{
		// Источники есть, а солнца нет — ночь: запасное солнце только у уровня совсем без света, как в setBuffer
		color = XMFLOAT3( 0.0f, 0.0f, 0.0f );
	}
	direction = XMFLOAT3( -lightDirection.x, -lightDirection.y, -lightDirection.z );
}

int DMLightDriver::sunLightIndex() const
{
	return m_sunIndex;
}

DMLight::ShadowSettings DMLightDriver::sunShadows() const
{
	const DMLight* light = sun();
	return light ? light->shadowSettings() : DMLight::ShadowSettings();
}

float DMLightDriver::sunIlluminance() const
{
	XMFLOAT3 direction;
	XMFLOAT3 color;
	directionalLight( direction, color );
	return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

const DMLightDriver::LightList& DMLightDriver::lights() const
{
	return m_light_list;
}

PropertyContainer* DMLightDriver::properties()
{
	return &m_properties;
}

XMFLOAT3 DMLightDriver::fallbackDirection()
{
	const float component = 1.0f / sqrtf( 3.0f );
	return XMFLOAT3( -component, -component, component );
}
