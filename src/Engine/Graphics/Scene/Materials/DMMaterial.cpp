#include "DMMaterial.h"



DMMaterial::DMMaterial( uint32_t id, const std::string& name ) : DMResource(id,name)
{
}


DMMaterial::~DMMaterial()
{
}

void DMMaterial::addShader( uint32_t id, float range )
{
	m_shaders.insert( {range, id} );
}

uint32_t DMMaterial::shader( float range )
{
	for( auto pair : m_shaders )
	{
		if( pair.first < range )
			return pair.second;
	}
	return UINT32_MAX;
}