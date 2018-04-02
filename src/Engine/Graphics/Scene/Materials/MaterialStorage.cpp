#include "MaterialStorage.h"

namespace GS
{

MaterialStorage::MaterialStorage( const std::wstring& path ) : DMResourceStorage( path )
{

}

MaterialStorage::~MaterialStorage()
{

}

bool MaterialStorage::load( const std::wstring& name )
{
	if( exists( name ) )
		return true;

	std::wstring fullPath = path() + L"\\" + name;

	//insertResource( name, std::move( m_modelLoader.loadFromFile( fullPath, nextId() ) ) );

	return true;
}

}