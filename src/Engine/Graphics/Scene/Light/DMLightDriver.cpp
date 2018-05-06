
#include "DMLightDriver.h"


DMLightDriver::DMLightDriver()
{
	
}


DMLightDriver::~DMLightDriver(void)
{	
}

bool DMLightDriver::Initialize()
{	
	m_structBuffer.CreateBuffer( sizeof( LightBuffer ), 32 );
	m_lightParamBuffer.reserve( 32 );

	return true;
}

void DMLightDriver::addLight( const DMLight& light )
{
	m_light_list.push_back( light );
}

uint32_t DMLightDriver::setBuffer( int8_t slot, SRVType type )
{	
	LightBuffer lightBuffer;
	m_lightParamBuffer.clear();
	for( auto& light : m_light_list )
	{	
		D3DXMATRIX mat = light.m_transformBuffer.resultMatrix();
		lightBuffer.lightPos = D3DXVECTOR3( mat._41, mat._42, mat._43 );
		lightBuffer.lightType = (int)light.type();
		lightBuffer.lightColor = light.color();
		lightBuffer.attenuation = light.attenuation;
		lightBuffer.lightDir = light.direction();
		m_lightParamBuffer.push_back( lightBuffer );
	}

	if( m_lightParamBuffer.empty() )
	{	
		lightBuffer.lightPos = D3DXVECTOR3( 1.0f, 1.0f, -1.0f );
		lightBuffer.lightType = 0;
		lightBuffer.lightColor = D3DXVECTOR3( 1.0f, 1.0f, 1.0f );
		lightBuffer.attenuation = 1000.0f;
		m_lightParamBuffer.push_back( lightBuffer );
	}

	m_structBuffer.UpdateData( &m_lightParamBuffer[0], m_lightParamBuffer.size() * sizeof( LightBuffer ) );

	m_structBuffer.setToSlot( slot, type );

	return m_lightParamBuffer.size();
 }


DMLightDriver::LightList& DMLightDriver::lights()
{
	return m_light_list;
}