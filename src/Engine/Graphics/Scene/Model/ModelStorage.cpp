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

	if( !model )
		return false;

	insertResource( model->name(), std::move( model ) );

	return true;
}

bool ModelStorage::clone( const std::string& source, const std::string& newName )
{
	if( !exists( source ) )
		return false;

	std::unique_ptr<DMModel> newModel( new DMModel( nextId(), newName ) );
	get( source )->copyTo( *newModel );
	return insertResource( newName, std::move( newModel ) );
}

}