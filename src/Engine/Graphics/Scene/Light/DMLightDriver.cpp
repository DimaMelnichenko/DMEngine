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
	m_structBuffer.createBuffer( sizeof( LightBuffer ), 32 );
	m_lightParamBuffer.reserve( 32 );

	return true;
}

void DMLightDriver::addLight( DMLight&& light )
{
	m_light_list.push_back( std::move(light) );
}

uint32_t DMLightDriver::setBuffer( int8_t slot, SRVType type )
{	
	LightBuffer lightBuffer;
	m_lightParamBuffer.clear();

	std::sort( m_light_list.begin(), m_light_list.end(), []( const auto& a, const auto& b )
	{
		return (int)a.type() < (int)b.type();
	} );

	for( auto& light : m_light_list )
	{	
		if( !light.enabled() )
			continue;

		XMMATRIX mat = light.m_transformBuffer.resultMatrix();
		XMFLOAT4 pos;
		XMStoreFloat4( &pos, mat.r[3] );
		lightBuffer.lightPos = XMFLOAT3( pos.x, pos.y, pos.z );
		lightBuffer.lightType = (int)light.type();
		lightBuffer.lightColor = light.color();
		lightBuffer.attenuation = light.m_attenuation;
		lightBuffer.lightDir = light.direction();
		m_lightParamBuffer.push_back( lightBuffer );
	}

	if( m_lightParamBuffer.empty() )
	{	
		lightBuffer.lightPos = XMFLOAT3( 1.0f, 1.0f, -1.0f );
		lightBuffer.lightType = 0;
		lightBuffer.lightColor = XMFLOAT3( 1.0f, 1.0f, 1.0f );
		lightBuffer.attenuation = 1000.0f;
		m_lightParamBuffer.push_back( lightBuffer );
	}

	m_structBuffer.updateData( &m_lightParamBuffer[0], m_lightParamBuffer.size() * sizeof( LightBuffer ) );

	m_structBuffer.setToSlot( slot, type );

	return m_lightParamBuffer.size();
 }


DMLightDriver::LightList& DMLightDriver::lights()
{
	return m_light_list;
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

			XMFLOAT3 vec = strToVec3( lightFile.get<std::string>( section, "Color" ) );
			light.setColor( vec );

			vec = strToVec3( lightFile.get<std::string>( section, "Position" ) );
			light.m_transformBuffer.setPosition( vec );

			light.m_attenuation = lightFile.get<float>( section, "Fade" );

			m_light_list.push_back( std::move( light ) );
		}

	}
	catch( std::exception& )
	{
		return false;
	}

	return true;
}