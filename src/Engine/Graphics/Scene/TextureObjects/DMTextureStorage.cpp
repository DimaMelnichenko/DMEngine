#include "DMTextureStorage.h"

DMTextureStorage::DMTextureStorage( const std::wstring& path ) : DMResourceStorage( path )
{

}

DMTextureStorage::~DMTextureStorage()
{

}

bool DMTextureStorage::load( const std::wstring& name )
{
	DMTexture texture( nextId() );
	insertResource( name, std::move( texture ) );

	return true;
}
