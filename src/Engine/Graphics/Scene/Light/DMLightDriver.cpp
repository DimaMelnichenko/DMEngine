#include "DMLightDriver.h"
#include "ResourceMetaFile.h"
#include <algorithm>


DMLightDriver::DMLightDriver()
{
	
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

uint32_t DMLightDriver::setBuffer( int8_t slot, SRVType type )
{	
	LightBuffer lightBuffer = {};
	m_lightParamBuffer.clear();

	// Устойчивая сортировка: порядок источников одного типа — как в файле, солнце — первый направленный
	std::stable_sort( m_light_list.begin(), m_light_list.end(), []( const auto& a, const auto& b )
	{
		return (int)a.type() < (int)b.type();
	} );

	for( auto& light : m_light_list )
	{	
		if( !light.enabled() || m_lightParamBuffer.size() == maxLights )
			continue;

		lightBuffer.position = light.position();
		lightBuffer.type = (int)light.type();
		lightBuffer.direction = light.direction();
		lightBuffer.attenuationRadius = light.attenuationRadius();
		lightBuffer.color = light.color();
		lightBuffer.cosOuterCone = cosf( XMConvertToRadians( light.outerConeAngle() ) );
		lightBuffer.cosInnerCone = cosf( XMConvertToRadians( light.innerConeAngle() ) );
		m_lightParamBuffer.push_back( lightBuffer );
	}

	if( m_lightParamBuffer.empty() )
	{	
		lightBuffer = {};
		lightBuffer.type = DMLight::Dir;
		lightBuffer.direction = fallbackDirection();
		lightBuffer.color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
		m_lightParamBuffer.push_back( lightBuffer );
	}

	m_structBuffer.updateData( &m_lightParamBuffer[0], m_lightParamBuffer.size() * sizeof( LightBuffer ) );

	m_structBuffer.setToSlot( slot, type );

	return m_lightParamBuffer.size();
 }


bool DMLightDriver::loadFromFile( const std::string& file )
{
	ResourceMetaFile lightFile( file );

	try
	{
		int32_t count = lightFile.get<int32_t>( "General", "Count" );

		for( int counter = 0; counter < count; ++counter )
		{
			std::string section = "Light" + std::to_string( counter );

			DMLight::LightType type = DMLight::strToType( lightFile.get<std::string>( section, "Type" ) );

			DMLight light( type );

			XMFLOAT3 vec;
			if( !strToVec3( lightFile.get<std::string>( section, "Color" ), vec ) )
				return false;

			light.setColor( vec );

			// Направленному нужно только направление, точечному — только положение
			if( type != DMLight::Dir )
			{
				if( !strToVec3( lightFile.get<std::string>( section, "Position" ), vec ) )
					return false;
				light.setPosition( vec );
				light.setAttenuationRadius( lightFile.get<float>( section, "AttenuationRadius" ) );
			}

			if( type != DMLight::Point )
			{
				if( !strToVec3( lightFile.get<std::string>( section, "Direction" ), vec ) )
					return false;
				light.setDirection( vec );
			}

			if( type == DMLight::Spot )
			{
				const std::string inner = lightFile.get<std::string>( section, "InnerConeAngle" );
				const std::string outer = lightFile.get<std::string>( section, "OuterConeAngle" );
				light.setConeAngles( inner.empty() ? light.innerConeAngle() : std::stof( inner ),
									 outer.empty() ? light.outerConeAngle() : std::stof( outer ) );
			}

			m_light_list.push_back( std::move( light ) );
		}

	}
	catch( std::exception& )
	{
		return false;
	}

	return true;
}

void DMLightDriver::directionalLight( XMFLOAT3& direction, XMFLOAT3& color ) const
{
	XMFLOAT3 lightDirection = fallbackDirection();
	color = XMFLOAT3( 1.0f, 1.0f, 1.0f );
	for( const auto& light : m_light_list )
	{
		if( light.enabled() && light.type() == DMLight::Dir )
		{
			lightDirection = light.direction();
			color = light.color();
			break;
		}
	}
	direction = XMFLOAT3( -lightDirection.x, -lightDirection.y, -lightDirection.z );
}

int DMLightDriver::sunLightIndex() const
{
	for( const auto& light : m_light_list )
	{
		if( light.enabled() )
			return light.type() == DMLight::Dir ? 0 : -1;
	}
	return -1;
}

XMFLOAT3 DMLightDriver::fallbackDirection()
{
	const float component = 1.0f / sqrtf( 3.0f );
	return XMFLOAT3( -component, -component, component );
}
