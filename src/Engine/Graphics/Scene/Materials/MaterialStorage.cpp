#include "MaterialStorage.h"

namespace GS
{

MaterialStorage::MaterialStorage( const std::string& path ) : DMResourceStorage( path )
{

}

MaterialStorage::~MaterialStorage()
{

}

bool MaterialStorage::load( const std::string& name )
{
	if( exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + name;

	std::unique_ptr<Material> material;
	material.reset( m_materialLoader.loadFromFile( fullPath, nextId(), name ) );
	if( material == nullptr )
		return false;
	insertResource( name, std::move( material ) );

	return true;
}

}