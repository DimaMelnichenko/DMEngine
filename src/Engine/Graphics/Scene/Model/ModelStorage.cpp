#include "ModelStorage.h"

namespace GS
{

ModelStorage::ModelStorage( const std::string& path ) : DMResourceStorage( path )
{

}

ModelStorage::~ModelStorage()
{

}

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

}