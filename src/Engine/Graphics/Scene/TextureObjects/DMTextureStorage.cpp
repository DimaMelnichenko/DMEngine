#include "DMTextureStorage.h"

namespace GS
{

DMTextureStorage::DMTextureStorage( const std::string& path ) : DMResourceStorage( path )
{

}

DMTextureStorage::~DMTextureStorage()
{

}

bool DMTextureStorage::load( const std::string& name )
{
	if( exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + name;

	ID3D11ShaderResourceView* srv = m_textureLoader.loadFromFile( fullPath.data() );
	if( !srv )
		return false;

	std::unique_ptr<DMTexture>  texture( new DMTexture( nextId(), srv ));
	
	insertResource( name, std::move( texture ) );

	return true;
}

}