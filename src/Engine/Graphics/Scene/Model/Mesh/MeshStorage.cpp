#include "MeshStorage.h"

namespace GS
{

MeshStorage::MeshStorage( const std::wstring& path ) : DMResourceStorage( path )
{

}

MeshStorage::~MeshStorage()
{

}

bool MeshStorage::load( const std::wstring& name )
{

	if( exists( name ) )
		return true;

	std::wstring fullPath = path() + L"\\" + name;

	insertResource( name, std::move( m_meshLoader.loadFromFile<MeshVertexData::PTNTB>( fullPath, nextId() ) ) );

	return true;
}

}