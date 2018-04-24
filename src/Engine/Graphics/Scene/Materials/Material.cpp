#include "Material.h"

namespace GS
{

Material::Material( uint32_t id, const std::string& name ) : DMResource( id, name )
{
}

Material& Material::operator=( Material&& other ) 
{
	std::swap( m_shader, other.m_shader );
	return *this;
}

Material::Material( Material&& other ) : DMResource( std::move( other ) )
{
	*this = std::move( other );
}

Material::~Material()
{
}

}