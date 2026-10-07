#include "MaterialStorage.h"
#include "ColorMaterial.h"
#include "TextureMaterial.h"
#include "PBRMaterial.h"
#include "Logger\Logger.h"

namespace GS
{

MaterialStorage::MaterialStorage( const std::string& path ) : DMResourceStorage( path )
{

}

MaterialStorage::~MaterialStorage()
{

}

bool MaterialStorage::createMaterial( uint32_t id, const std::string& name, const std::string& matClass )
{
	if( exists( id ) )
		return true;

	std::unique_ptr<Material> material;
	if( matClass == "Color" )
		material = std::make_unique<ColorMaterial>( id, name );
	else if( matClass == "PBR" )
		material = std::make_unique<PBRMaterial>( id, name );
	else if( matClass == "Texture" )
		material = std::make_unique<TextureMaterial>( id, name );

	if( !material )
	{
		LOG( "Unknown material class '" + matClass + "' of material " + name );
		return false;
	}

	material->setLayoutDesc( material->initLayouts() );
	insertResource( std::move( material ) );

	return true;
}

}