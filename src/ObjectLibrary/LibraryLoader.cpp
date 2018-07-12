#include "LibraryLoader.h"
#include "System.h"
#include <iostream>



LibraryLoader::LibraryLoader()
	: m_db(0)
{
}


LibraryLoader::~LibraryLoader()
{
}

bool LibraryLoader::init()
{
	try
	{
		if( m_db == nullptr )
			m_db = new SQLite::Database( "base.db3", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE );
	}
	catch( std::exception e )
	{
		std::cout << e.what();
		return false;
	}

	
	std::string msg( m_db->getErrorMsg() );
	
	return true;
}

bool LibraryLoader::loadTexture( uint32_t idTexture )
{
	if( GS::System::textures().exists( idTexture ) )
		return true;

	SQLite::Statement queryTexture( *m_db, "SELECT id, name, file FROM Textures where id = :id" );
	queryTexture.bind( ":id", idTexture );
	while( queryTexture.executeStep() )
	{
		GS::System::textures().load( queryTexture.getColumn( 0 ), queryTexture.getColumn( 1 ), queryTexture.getColumn( 2 ) );
	}

	return true;
}

bool LibraryLoader::loadMaterial( uint32_t idMaterial )
{
	if( GS::System::materials().exists( idMaterial ) )
		return true;

	SQLite::Statement query( *m_db, "SELECT id, name, vs, gs, ps file FROM Materials where id = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{
		GS::System::materials().load( query.getColumn( 0 ), query.getColumn( 1 ), query.getColumn( 2 ), query.getColumn( 3 ), query.getColumn( 4 ) );
	}

	return true;
}

bool LibraryLoader::loadMesh( uint32_t idMesh )
{
	if( GS::System::meshes().exists( idMesh ) )
		return true;

	SQLite::Statement query( *m_db, "SELECT id, name, file FROM Meshes where id = :id" );
	query.bind( ":id", idMesh );
	while( query.executeStep() )
	{
		GS::System::meshes().load( query.getColumn( 0 ), query.getColumn( 1 ), query.getColumn( 2 ) );
	}

	return true;
}

bool LibraryLoader::loadModelWithLOD( uint32_t idModel )
{
	if( GS::System::models().exists( idModel ) )
		return true;

	SQLite::Statement query( *m_db, "SELECT id, name FROM Models where id = :id" );
	query.bind( ":id", idModel );
	while( query.executeStep() )
	{	
		if( !GS::System::models().createModel( query.getColumn( 0 ), query.getColumn( 1 ) ) )
			return false;
	}

	GS::DMModel* model = GS::System::models().get( idModel ).get();
	if( !model )
		return false;
	
	SQLite::Statement queryLOD( *m_db, "SELECT id, lod, range, material_id, mesh_id, render FROM ModelLOD where model_id = :id order by lod" );
	queryLOD.bind( ":id", idModel );
	while( queryLOD.executeStep() )
	{
		if( !loadMaterial( queryLOD.getColumn( "material_id" ) ) )
			return false;

		if( !loadMesh( queryLOD.getColumn( "mesh_id" ) ) )
			return false;

		GS::DMModel::LodBlock block;
		block.material = queryLOD.getColumn( "material_id" );
		block.mesh = queryLOD.getColumn( "mesh_id" );
		block.isRender = queryLOD.getColumn( "render" ).getInt();
		if( !loadMaterialParams( queryLOD.getColumn( "id" ), block.params ) )
			return false;

		model->addLod( queryLOD.getColumn("range").getDouble(), block );
	}

	return true;
}

bool LibraryLoader::loadMaterialParams( uint32_t idLod, GS::ParamSet& paramSet )
{
	SQLite::Statement query( *m_db, "SELECT name, type, value FROM MaterialParamsView where lod_id = :id" );
	query.bind( ":id", idLod );
	while( query.executeStep() )
	{
		std::string paramName = query.getColumn( "name" );
		std::string paramType = query.getColumn( "type" );
		std::string paramValue = query.getColumn( "value" );

		if( paramType == "texture" )
		{
			uint32_t texture_id = static_cast<uint32_t>( std::stol( paramValue ) );
			if( !loadTexture( texture_id ) )
				return false;
			paramSet.insert( { paramName, GS::Parameter( texture_id ) } );
		}
		else if( paramType == "vec4" )
		{
			paramSet.insert( { paramName, GS::Parameter( strToVec4( paramValue ) ) } );
		}
	}

	return true;
}