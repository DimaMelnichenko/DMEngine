#include "Material.h"

namespace GS
{

Material::Material( uint32_t id, const std::string& name ) : DMResource( id, name )
{
}


Material::~Material()
{
}

void Material::addShader( uint32_t id, float range )
{
	m_shaders.insert( { range, id } );
}

uint32_t Material::shader( float range )
{
	for( auto pair : m_shaders )
	{
		if( pair.first < range )
			return pair.second;
	}
	return UINT32_MAX;
}

}