#include "DMTextureStorage.h"

DMTextureStorage::DMTextureStorage( const std::wstring& path ) : DMResourceStorage( path )
{

}

DMTextureStorage::~DMTextureStorage()
{

}

bool DMTextureStorage::load( const std::wstring& name )
{
	if( exists(name) )
		return true;

	std::wstring fullPath = path() + L"\\" + name;

	DMTexture texture( nextId(), m_textureLoader.loadFromFile( fullPath.data() ) );

	insertResource( name, std::move( texture ) );

	return true;
}
