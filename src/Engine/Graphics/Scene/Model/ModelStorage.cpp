#include "ModelStorage.h"

namespace GS
{

ModelStorage::ModelStorage( const std::wstring& path ) : DMResourceStorage( path )
{

}

ModelStorage::~ModelStorage()
{

}

bool ModelStorage::load( const std::wstring& name )
{
	if( exists( name ) )
		return true;

	std::wstring fullPath = path() + L"\\" + name;

	insertResource( name, std::move( m_modelLoader.loadFromFile( fullPath, nextId() ) ) );

	return true;
}

}