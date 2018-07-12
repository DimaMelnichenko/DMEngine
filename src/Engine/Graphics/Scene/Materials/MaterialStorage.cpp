#include "MaterialStorage.h"

namespace GS
{

MaterialStorage::MaterialStorage( const std::string& path ) : DMResourceStorage( path )
{

}

MaterialStorage::~MaterialStorage()
{

}

bool MaterialStorage::load( uint32_t id, const std::string& name, const std::string& vs, const std::string& gs, const std::string& ps )
{
	if( exists( id ) )
		return true;

	std::string fullPath = path() + "\\";

	std::unique_ptr<Material> material;
	material.reset( m_materialLoader.loadFromFile( id, name, 
												   vs.empty() ? "" : fullPath + vs, 
												   gs.empty() ? "" : fullPath + gs,
												   ps.empty() ? "" : fullPath + ps ) );
	if( material == nullptr )
		return false;
	insertResource( std::move( material ) );

	return true;
}

}