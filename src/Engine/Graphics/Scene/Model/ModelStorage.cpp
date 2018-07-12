#include "ModelStorage.h"

namespace GS
{

ModelStorage::ModelStorage( const std::string& path ) : DMResourceStorage( path )
{

}

ModelStorage::~ModelStorage()
{

}
/*
bool ModelStorage::load( const std::string& name )
{
	uint32_t id = nextId();

	if( exists( name ) )
		return true;

	std::string fullPath = path() + "\\" + name;

	std::unique_ptr<DMModel> model;	
	model.reset( m_modelLoader.loadFromFile( fullPath, id ) );

	if( !model )
		return false;

	insertResource( std::move( model ) );

	return true;
}*/

bool ModelStorage::createModel( uint32_t id, const std::string& name )
{
	if( exists( id ) )
		return true;

	std::unique_ptr<DMModel> model( new DMModel( id, name ) );

	if( !model )
		return false;

	insertResource( std::move( model ) );

	return true;
}

bool ModelStorage::clone( const std::string& source, const std::string& newName )
{
	uint32_t id = nextId();

	if( !exists( source ) )
		return false;

	std::unique_ptr<DMModel> newModel( new DMModel( id, newName ) );
	get( source )->copyTo( *newModel );
	return insertResource( std::move( newModel ) );
}

}