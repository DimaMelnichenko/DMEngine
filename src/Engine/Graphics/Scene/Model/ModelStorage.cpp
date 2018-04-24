#include "ModelStorage.h"

namespace GS
{

ModelStorage::ModelStorage( const std::string& path ) : DMResourceStorage( path )
{

}

ModelStorage::~ModelStorage()
{

}

bool ModelStorage::load( const std::string& name )
{
	if( exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + name;

	std::unique_ptr<DMModel> model;
	model.reset( m_modelLoader.loadFromFile( fullPath, nextId() ) );
	insertResource( name, std::move( model ) );

	return true;
}

}