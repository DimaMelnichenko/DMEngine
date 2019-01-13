#include "LibraryLoader.h"
#include "System.h"
#include <iostream>
#include <chrono>

#include "Dictionaries\ParameterType.h"
#include "DBConnector.h"
#include "Logger\Logger.h"




LibraryLoader::LibraryLoader()
{
}


LibraryLoader::~LibraryLoader()
{
}

bool LibraryLoader::loadTexture( uint32_t idTexture )
{	
	
	if( GS::System::textures().exists( idTexture ) )
		return true;

	std::string queryText;
	if( idTexture == -1 )
	{
		LOG( "Load all textures" );
		queryText = "SELECT id, name, file, generate_mipmap, sRGB FROM Textures";
	}
	else
	{
		queryText = "SELECT id, name, file, generate_mipmap, sRGB FROM Textures where id = :id";
	}

	SQLite::Statement queryTexture( dbConnect().db(), queryText );
	if( idTexture != -1 )
		queryTexture.bind( ":id", idTexture );

	while( queryTexture.executeStep() )
	{
		auto startTime = std::chrono::high_resolution_clock::now();
		uint32_t id = queryTexture.getColumn( 0 );
		const std::string& textureName = queryTexture.getColumn( 1 ).getString();
		const std::string& fileName = queryTexture.getColumn( 2 ).getString();
		
		if( !GS::System::textures().load( id, textureName, fileName, queryTexture.getColumn( 3 ).getInt(), queryTexture.getColumn( 4 ).getInt() ) )
		{
			//return false;
			std::string msg = "Can`t load texture " + textureName;
			throw std::exception( msg.data() );
		}
		auto count = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - startTime ).count() / 1000;
		LOG( "Load texture for ms: " + std::to_string( count ) + "; id = " + std::to_string( id ) + ", name = " + textureName + ", file = " + fileName );
	}

	return true;
}

bool LibraryLoader::loadMaterial( const std::string& materialName )
{
	
	if( GS::System::materials().exists( materialName ) )
		return true;

	LOG( "Load material: " + materialName );

	SQLite::Statement query( dbConnect().db(), "SELECT id, name FROM Materials where name = :name" );
	query.bind( ":name", materialName );
	while( query.executeStep() )
	{
		return loadMaterial( query.getColumn( 0 ).getInt() );
	}

	LOG( "Not found material: " + materialName );

	return false;
}

bool LibraryLoader::loadMaterialInstance( uint32_t idMaterialInstance )
{


	return true;
}

bool LibraryLoader::loadMaterial( uint32_t idMaterial )
{
	
	if( GS::System::materials().exists( idMaterial ) )
		return true;

	LOG( "Load material: " + std::to_string( idMaterial ) );

	SQLite::Statement query( dbConnect().db(), "SELECT id, name, class file FROM Materials where id = :id" );
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
	SQLite::Statement query( dbConnect().db(), "SELECT id_material, param_name, id_type FROM MaterialParameterDefView where id_material = :id" );
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
	
	SQLite::Statement query( dbConnect().db(), "SELECT material_id, file, type, define FROM MaterialShaderView where material_id = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{	
		std::string fullPath = GS::System::materials().path() + "\\" + query.getColumn( 1 ).getString();
		LOG( "Load shader: " + fullPath );
		if( !shader->addShaderPassFromFile( static_cast<SRVType>( query.getColumn( 2 ).getInt() ), "main", fullPath, query.getColumn( 3 ) ) )
			return false;
	}

	return true;
}

bool LibraryLoader::loadMesh( uint32_t idMesh )
{
	if( GS::System::meshes().exists( idMesh ) )
		return true;

	LOG( "Load mesh: " + std::to_string( idMesh ) );

	SQLite::Statement query( dbConnect().db(), "SELECT id, name, file FROM Meshes where id = :id" );
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

	LOG( "Load model: " + std::to_string( idModel ) );

	SQLite::Statement query( dbConnect().db(), "SELECT id, name FROM Models where id = :id" );
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
	
	SQLite::Statement queryLOD( dbConnect().db(), "SELECT id, lod, range, material_id, mesh_id, render, material_instance_id FROM ModelProperties where model_id = :id order by lod" );
	queryLOD.bind( ":id", idModel );
	while( queryLOD.executeStep() )
	{
		uint32_t id_material = queryLOD.getColumn( "material_instance_id" ).getInt();
		if( !loadMaterial( id_material ) )
			return false;

		if( !loadMesh( queryLOD.getColumn( "mesh_id" ) ) )
			return false;

		GS::DMModel::LodBlock block;
		block.material = queryLOD.getColumn( "material_id" );
		block.mesh = queryLOD.getColumn( "mesh_id" );
		block.isRender = queryLOD.getColumn( "render" ).getInt();
		block.params = GS::System::materials().get( id_material )->m_parameters;
		if( !loadMaterialParams( queryLOD.getColumn( "material_instance_id" ).getInt(), block.params ) )
			return false;

		model->addLod( queryLOD.getColumn("range").getDouble(), block );
	}

	return true;
}

bool LibraryLoader::loadMaterialParams( uint32_t idInstance, GS::ParamSet& paramSet )
{
	SQLite::Statement query( dbConnect().db(), "SELECT id_instance, param_name, type, value FROM MaterialParamsValueView where id_instance = :instance" );
	query.bind( ":instance", idInstance );
	while( query.executeStep() )
	{
		std::string paramName = query.getColumn( "param_name" );
		std::string paramType = query.getColumn( "type" );
		std::string paramValue = query.getColumn( "value" );
		paramSet[paramName].m_id = query.getColumn( "id_instance" ).getInt();

		switch( paramSet[paramName].valueType() )
		{
			case ParameterType::float4: // vec4
				paramSet[paramName].setValue( strToVec4( paramValue ) );
				break;
			case ParameterType::textureId: // texture id
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
					case ParameterType::float4:
						insertedValue = vec4ToStr( paramItem.second.vector() );
						break;
					case ParameterType::textureId:
						insertedValue = std::to_string( paramItem.second.textId() );
						break;
				}

				uint32_t idValue = paramItem.second.m_id;				
				try
				{
					SQLite::Statement query( dbConnect().db(), "UPDATE MaterialParameterValue SET value = :value WHERE id = :id" );
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