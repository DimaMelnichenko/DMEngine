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

	SQLite::Statement queryTexture( *m_db, "SELECT id, name, file, generate_mipmap FROM Textures where id = :id" );
	queryTexture.bind( ":id", idTexture );
	while( queryTexture.executeStep() )
	{
		if( !GS::System::textures().load( queryTexture.getColumn( 0 ), queryTexture.getColumn( 1 ), queryTexture.getColumn( 2 ), queryTexture.getColumn( 3 ).getInt() ) )
			return false;
	}

	return true;
}

bool LibraryLoader::loadMaterial( const std::string& materialName )
{
	if( GS::System::materials().exists( materialName ) )
		return true;

	SQLite::Statement query( *m_db, "SELECT id, name FROM Materials where name = :name" );
	query.bind( ":name", materialName );
	while( query.executeStep() )
	{
		return loadMaterial( query.getColumn( 0 ).getInt() );
	}

	return false;
}

bool LibraryLoader::loadMaterial( uint32_t idMaterial )
{
	if( GS::System::materials().exists( idMaterial ) )
		return true;

	SQLite::Statement query( *m_db, "SELECT id, name, class file FROM Materials where id = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{
		if( !GS::System::materials().createMaterial( query.getColumn( 0 ), query.getColumn( 1 ), query.getColumn( 2 ) ) )
			return false;
	}

	if( !GS::System::materials().exists( idMaterial ) )
		return false;

	loadMaterialParamDef( idMaterial, GS::System::materials().get( idMaterial )->m_parameters );

	GS::DMShader* shader = GS::System::materials().get( idMaterial )->m_shader.get();

	if( !loadShader( idMaterial, shader ) )
		return false;

	if( !shader->initialize() )
		return false;

	return true;
}

bool LibraryLoader::loadMaterialParamDef( uint32_t idMaterial, GS::ParamSet& paramSet )
{
	SQLite::Statement query( *m_db, "SELECT material_id, name, type_id FROM MaterialParameterDefView where material_id = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{
		switch( query.getColumn( 2 ).getInt() )
		{
			case 1: // vec4
				paramSet.insert( { query.getColumn( 1 ).getString(), GS::Parameter( XMFLOAT4() ) } );
				break;
			case 2: // texture id
				paramSet.insert( { query.getColumn( 1 ).getString(), GS::Parameter( uint32_t(0) ) } );
				break;
		}
	}

	return true;
}

bool LibraryLoader::loadShader( uint32_t idMaterial, GS::DMShader* shader )
{
	SQLite::Statement query( *m_db, "SELECT material_id, file, type, define FROM MaterialShaderView where material_id = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{
		std::string fullPath = GS::System::materials().path() + "\\" + query.getColumn( 1 ).getString();
		if( !shader->addShaderPass( static_cast<SRVType>( query.getColumn( 2 ).getInt() ), "main", fullPath, query.getColumn( 3 ) ) )
			return false;
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
		if( !GS::System::meshes().load( query.getColumn( 0 ), query.getColumn( 1 ), query.getColumn( 2 ) ) )
			return false;
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

	if( !GS::System::models().exists( idModel ) )
		return false;

	GS::DMModel* model = GS::System::models().get( idModel ).get();
	if( !model )
		return false;
	
	SQLite::Statement queryLOD( *m_db, "SELECT id, lod, range, material_id, mesh_id, render, mat_param_id FROM ModelLOD where model_id = :id order by lod" );
	queryLOD.bind( ":id", idModel );
	while( queryLOD.executeStep() )
	{
		uint32_t id_material = queryLOD.getColumn( "material_id" ).getInt();
		if( !loadMaterial( id_material ) )
			return false;

		if( !loadMesh( queryLOD.getColumn( "mesh_id" ) ) )
			return false;

		GS::DMModel::LodBlock block;
		block.material = queryLOD.getColumn( "material_id" );
		block.mesh = queryLOD.getColumn( "mesh_id" );
		block.isRender = queryLOD.getColumn( "render" ).getInt();
		block.params = GS::System::materials().get( id_material )->m_parameters;
		if( !loadMaterialParams( queryLOD.getColumn( "mat_param_id" ).getInt(), block.params ) )
			return false;

		model->addLod( queryLOD.getColumn("range").getDouble(), block );
	}

	return true;
}

bool LibraryLoader::loadMaterialParams( uint32_t idValue, GS::ParamSet& paramSet )
{
	SQLite::Statement query( *m_db, "SELECT id, name, type, value FROM MaterialParamsValueView where group_id = :group" );
	query.bind( ":group", idValue );
	while( query.executeStep() )
	{
		std::string paramName = query.getColumn( "name" );
		std::string paramType = query.getColumn( "type" );
		std::string paramValue = query.getColumn( "value" );
		paramSet[paramName].m_id = query.getColumn( "id" ).getInt();

		switch( paramSet[paramName].valueType() )
		{
			case GS::Parameter::float4: // vec4
				paramSet[paramName].setValue( strToVec4( paramValue ) );
				break;
			case GS::Parameter::textureId: // texture id
				paramSet[paramName].setValue( static_cast<uint32_t>( std::stol( paramValue ) ) );
				break;
			default:
				return false;
		}
	}

	return true;
}

void LibraryLoader::save()
{	
	std::string insertedValue;

	for( auto& pair : GS::System::models() )
	{
		GS::DMModel* model = pair.second.get();
		for( uint32_t i = 0; i < model->lodCount(); ++i )
		{
			GS::DMModel::LodBlock* lodBlock = model->getLodById( i );
			for( auto& paramItem : lodBlock->params )
			{
				switch( paramItem.second.valueType() )
				{
					case GS::Parameter::ValueType::float4:
						insertedValue = vec4ToStr( paramItem.second.vector() );
						break;
					case GS::Parameter::ValueType::textureId:
						insertedValue = std::to_string( paramItem.second.textId() );
						break;
				}

				uint32_t idValue = paramItem.second.m_id;				
				try
				{
					SQLite::Statement query( *m_db, "UPDATE MaterialParameterValue SET value = :value WHERE id = :id" );
					query.bind( ":value", insertedValue );
					query.bind( ":id", idValue );
					query.exec();
				}
				catch( std::exception e )
				{
					std::cout << e.what() << std::endl;
				}
			}
		}

		
	}
}