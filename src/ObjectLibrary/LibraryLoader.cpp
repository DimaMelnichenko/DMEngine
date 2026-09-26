#include "LibraryLoader.h"
#include "System.h"
#include <iostream>
#include <chrono>

#include "DBConnector.h"
#include "Logger\Logger.h"




LibraryLoader::LibraryLoader()
{
}


LibraryLoader::~LibraryLoader()
{
}

bool LibraryLoader::loadAllTextures()
{
	LOG( "Load all textures" );
	SQLite::Statement queryTexture( dbConnect().db(), "SELECT id, name, file, generate_mipmap, sRGB FROM Textures" );
	return loadTextures( queryTexture );
}

bool LibraryLoader::loadTexture( uint32_t idTexture )
{
	if( GS::System::textures().exists( idTexture ) )
		return true;

	SQLite::Statement queryTexture( dbConnect().db(), "SELECT id, name, file, generate_mipmap, sRGB FROM Textures where id = :id" );
	queryTexture.bind( ":id", idTexture );
	return loadTextures( queryTexture );
}

bool LibraryLoader::loadTextures( SQLite::Statement& queryTexture )
{
	while( queryTexture.executeStep() )
	{
		auto startTime = std::chrono::high_resolution_clock::now();
		uint32_t id = queryTexture.getColumn( 0 );
		const std::string& textureName = queryTexture.getColumn( 1 ).getString();
		const std::string& fileName = queryTexture.getColumn( 2 ).getString();
		
		if( !GS::System::textures().load( id, textureName, fileName, queryTexture.getColumn( 3 ).getInt(), queryTexture.getColumn( 4 ).getInt() ) )
		{
			LOG( "Can`t load texture " + textureName + " (" + fileName + "), placeholder is used" );
			continue;
		}
		auto count = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - startTime ).count() / 1000;
		LOG( "Load texture for ms: " + std::to_string( count ) + "; id = " + std::to_string( id ) + ", name = " + textureName + ", file = " + fileName );
	}

	return true;
}

bool LibraryLoader::loadAllMaterials()
{
	std::vector<uint32_t> ids;
	SQLite::Statement query( dbConnect().db(), "SELECT id FROM Materials ORDER BY id" );
	while( query.executeStep() )
	{
		ids.push_back( query.getColumn( 0 ).getUInt() );
	}

	for( uint32_t id : ids )
	{
		if( !loadMaterial( id ) )
			return false;
	}

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

// Значение параметра материала из строки БД: "true", "0.5", "1.0,1.0,1.0,1.0", id текстуры
static bool setParamFromString( Property& prop, const std::string& value )
{
	switch( prop.valueType() )
	{
		case ValueType::BOOL:
			prop.setData( value == "true" || value == "1" );
			return true;
		case ValueType::FLOAT:
			prop.setData( std::stof( value ) );
			return true;
		case ValueType::VECTOR2:
		{
			XMFLOAT2 vec;
			if( !strToVec2( value, vec ) )
				return false;
			prop.setData( vec );
			return true;
		}
		case ValueType::VECTOR3:
		{
			XMFLOAT3 vec;
			if( !strToVec3( value, vec ) )
				return false;
			prop.setData( vec );
			return true;
		}
		case ValueType::VECTOR4:
		{
			XMFLOAT4 vec;
			if( !strToVec4( value, vec ) )
				return false;
			prop.setData( vec );
			return true;
		}
		case ValueType::INT:
			prop.setData( static_cast<int32_t>( std::stoi( value ) ) );
			return true;
		case ValueType::UINT:
			prop.setData( static_cast<uint32_t>( std::stoul( value ) ) );
			return true;
	}

	return false;
}

bool LibraryLoader::loadMaterialParamDef( uint32_t idMaterial, PropertyContainer& paramSet )
{
	SQLite::Statement query( dbConnect().db(), "SELECT id_material, param_name, value_type, control_type, low, high, default_value FROM MaterialParameterDefView where id_material = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{
		ValueType valueType = static_cast<ValueType>( query.getColumn( "value_type" ).getInt() );
		std::string name = query.getColumn( "param_name" ).getString();
		Property* prop = nullptr;

		switch( valueType )
		{
			case ValueType::BOOL:
				prop = paramSet.insert( name, false );
				break;
			case ValueType::FLOAT:
				prop = paramSet.insert( name, 0.0f );
				break;
			case ValueType::VECTOR2:
				prop = paramSet.insert( name, XMFLOAT2() );
				break;
			case ValueType::VECTOR3:
				prop = paramSet.insert( name, XMFLOAT3() );
				break;
			case ValueType::VECTOR4:
				prop = paramSet.insert( name, XMFLOAT4() );
				break;
			case ValueType::INT:
				prop = paramSet.insert( name, int32_t( 0 ) );
				break;
			case ValueType::UINT:
				prop = paramSet.insert( name, uint32_t( 0 ) );
				break;
		}

		if( prop )
		{
			prop->setControlType( static_cast<GUIControlType>( query.getColumn( "control_type" ).getInt() ) );
			prop->setLow( query.getColumn( "low" ).getDouble() );
			prop->setHigh( query.getColumn( "high" ).getDouble() );

			// Без default_value параметр остаётся нулевым
			const SQLite::Column defaultValue = query.getColumn( "default_value" );
			if( !defaultValue.isNull() && !setParamFromString( *prop, defaultValue.getString() ) )
				LOG( "Wrong default value of parameter " + name + " of material " + std::to_string( idMaterial ) );
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
	// Незагрузившийся меш запоминается: повторные запросы (например, с других LOD) сразу получают заглушку
	if( GS::System::meshes().exists( idMesh ) || m_failedMeshes.count( idMesh ) )
		return true;

	LOG( "Load mesh: " + std::to_string( idMesh ) );

	SQLite::Statement query( dbConnect().db(), "SELECT id, name, file, primitive FROM Meshes where id = :id" );
	query.bind( ":id", idMesh );
	while( query.executeStep() )
	{
		const std::string name = query.getColumn( "name" ).getString();
		const std::string file = query.getColumn( "file" ).getString();
		const std::string primitiveName = query.getColumn( "primitive" ).getString();

		if( !file.empty() && GS::System::meshes().load( idMesh, name, file ) )
			continue;

		// Файла нет или он не задан: подставляется примитив меша, а если примитива нет — куб из слота 0
		GS::MeshStorage::Primitive primitive;
		if( GS::MeshStorage::primitiveFromName( primitiveName, primitive ) &&
			GS::System::meshes().createPrimitive( idMesh, name, primitive ) )
		{
			if( !file.empty() )
				LOG( "Mesh " + std::to_string( idMesh ) + " is not loaded, placeholder " + primitiveName + " is used" );
			continue;
		}

		m_failedMeshes.insert( idMesh );
		LOG( "Mesh " + std::to_string( idMesh ) + " is not loaded, placeholder is used" );
	}

	return true;
}

bool LibraryLoader::loadLevel( const std::string& name, LevelDescription& level )
{
	try
	{
		const char* columns = "SELECT id, name, terrain, sky, particles FROM Levels ";
		SQLite::Statement query( dbConnect().db(), std::string( columns ) + ( name.empty() ? "ORDER BY id LIMIT 1" : "WHERE name = :name" ) );
		if( !name.empty() )
			query.bind( ":name", name );

		if( !query.executeStep() )
		{
			LOG( name.empty() ? std::string( "Table Levels is empty" ) : "Level '" + name + "' is not found in table Levels" );
			return false;
		}

		level.id = query.getColumn( "id" ).getUInt();
		level.name = query.getColumn( "name" ).getString();
		if( !query.getColumn( "terrain" ).isNull() )
			level.terrain = query.getColumn( "terrain" ).getUInt();
		if( !query.getColumn( "sky" ).isNull() )
			level.sky = query.getColumn( "sky" ).getUInt();
		const bool hasParticles = !query.getColumn( "particles" ).isNull();
		const uint32_t particlesId = query.getColumn( "particles" ).getUInt();

		SQLite::Statement queryModels( dbConnect().db(), "SELECT model, position, scale FROM LevelModels WHERE level = :level ORDER BY id" );
		queryModels.bind( ":level", level.id );
		while( queryModels.executeStep() )
		{
			LevelDescription::Model model;
			model.id = queryModels.getColumn( "model" ).getUInt();
			const std::string position = queryModels.getColumn( "position" ).getString();
			const std::string scale = queryModels.getColumn( "scale" ).getString();
			if( !position.empty() && !strToVec3( position, model.position ) )
				LOG( "Level model " + std::to_string( model.id ) + ": wrong position '" + position + "'" );
			if( !scale.empty() && !strToVec3( scale, model.scale ) )
				LOG( "Level model " + std::to_string( model.id ) + ": wrong scale '" + scale + "'" );
			level.models.push_back( model );
		}

		SQLite::Statement querySets( dbConnect().db(), "SELECT s.id, s.name, s.pass, s.two_sided, s.color_texture "
													   "FROM LevelScatterSets l JOIN ScatterSets s ON s.id = l.scatter_set "
													   "WHERE l.level = :level ORDER BY l.id" );
		querySets.bind( ":level", level.id );
		while( querySets.executeStep() )
		{
			LevelDescription::ScatterSet set;
			set.name = querySets.getColumn( "name" ).getString();
			set.pass = querySets.getColumn( "pass" ).getString();
			set.twoSided = querySets.getColumn( "two_sided" ).getInt() != 0;
			set.colorTexture = querySets.getColumn( "color_texture" ).getString();
			loadScatterLayers( querySets.getColumn( "id" ).getUInt(), set );
			level.scatterSets.push_back( std::move( set ) );
		}

		if( hasParticles )
		{
			SQLite::Statement queryParticles( dbConnect().db(), "SELECT material, texture, count_per_cell, area_size FROM Particles WHERE id = :id" );
			queryParticles.bind( ":id", particlesId );
			if( !queryParticles.executeStep() )
			{
				LOG( "Particles " + std::to_string( particlesId ) + " are not found in table Particles" );
				return false;
			}

			LevelDescription::Particles& particles = level.particles.emplace();
			particles.material = queryParticles.getColumn( "material" ).getString();
			particles.texture = queryParticles.getColumn( "texture" ).getString();
			particles.countPerCell = queryParticles.getColumn( "count_per_cell" ).getUInt();
			particles.areaSize = queryParticles.getColumn( "area_size" ).getUInt();
		}
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Can`t load level: " ) + e.what() );
		return false;
	}

	return true;
}

void LibraryLoader::loadScatterLayers( uint32_t idSet, LevelDescription::ScatterSet& set )
{
	SQLite::Statement query( dbConnect().db(), "SELECT model, model_lod, mask, cell_size, near_border, far_border, near_fallow, "
											   "far_fallow, size_multipler, jitter, rotation_x, rotation_y, rotation_z, align_to_terrain "
											   "FROM ScatterLayers WHERE scatter_set = :set ORDER BY layer" );
	query.bind( ":set", idSet );
	while( query.executeStep() )
	{
		auto value = [&query]( const char* column )
		{
			return static_cast<float>( query.getColumn( column ).getDouble() );
		};

		LevelDescription::ScatterLayer layer;
		layer.model = query.getColumn( "model" ).getUInt();
		layer.modelLod = static_cast<uint16_t>( query.getColumn( "model_lod" ).getUInt() );
		layer.mask = query.getColumn( "mask" ).getString();
		layer.params.cellSize = value( "cell_size" );
		layer.params.nearBorder = value( "near_border" );
		layer.params.farBorder = value( "far_border" );
		layer.params.nearFade = value( "near_fallow" );
		layer.params.farFade = value( "far_fallow" );
		layer.params.sizeMultipler = value( "size_multipler" );
		layer.params.jitter = value( "jitter" );
		layer.params.rotationRange = XMFLOAT3( XMConvertToRadians( value( "rotation_x" ) ), XMConvertToRadians( value( "rotation_y" ) ),
											   XMConvertToRadians( value( "rotation_z" ) ) );
		layer.params.alignToTerrain = query.getColumn( "align_to_terrain" ).getInt() ? 1.0f : 0.0f;
		set.layers.push_back( layer );
	}
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
		uint32_t idMaterial = queryLOD.getColumn( "material_id" ).getInt();
		if( !loadMaterial( idMaterial ) )
			return false;

		// Параметры LOD — экземпляр материала: определения от материала экземпляра, значения — его собственные.
		// Без экземпляра (NULL) — определения material_id со значениями по умолчанию
		uint32_t idInstance = queryLOD.getColumn( "material_instance_id" ).getInt();
		uint32_t idParamsMaterial = idMaterial;
		if( idInstance != 0 && !instanceMaterial( idInstance, idParamsMaterial ) )
		{
			LOG( "Material instance " + std::to_string( idInstance ) + " is not found, defaults of material " + std::to_string( idMaterial ) + " are used" );
			idInstance = 0;
		}
		if( !loadMaterial( idParamsMaterial ) )
			return false;

		if( !loadMesh( queryLOD.getColumn( "mesh_id" ) ) )
			return false;

		std::unique_ptr<GS::DMModel::LodBlock> block( new GS::DMModel::LodBlock() );
		block->material = idMaterial;
		block->mesh = queryLOD.getColumn( "mesh_id" );
		block->isRender = queryLOD.getColumn( "render" ).getInt();
		block->params = GS::System::materials().get( idParamsMaterial )->m_parameters;
		if( idInstance != 0 )
			loadMaterialParams( idInstance, block->params );

		model->properties()->addSubContainer( &block->params );
		model->addLod( queryLOD.getColumn("range").getDouble(), std::move(block) );
	}

	return true;
}

bool LibraryLoader::instanceMaterial( uint32_t idInstance, uint32_t& idMaterial )
{
	SQLite::Statement query( dbConnect().db(), "SELECT id_material FROM MaterialInstance where id_instance = :instance" );
	query.bind( ":instance", idInstance );
	if( !query.executeStep() || query.getColumn( 0 ).isNull() )
		return false;

	idMaterial = query.getColumn( 0 ).getInt();
	return true;
}

bool LibraryLoader::loadMaterialParams( uint32_t idInstance, PropertyContainer& paramSet )
{
	SQLite::Statement query( dbConnect().db(), "SELECT id_instance, param_name, value FROM MaterialParamsValueView where id_instance = :instance" );
	query.bind( ":instance", idInstance );
	while( query.executeStep() )
	{
		std::string paramName = query.getColumn( "param_name" );
		std::string paramValue = query.getColumn( "value" );

		// Значение для параметра, которого нет у материала экземпляра, пропускается: operator[] бросил бы std::out_of_range
		if( !paramSet.exists( paramName ) || !setParamFromString( paramSet[paramName], paramValue ) )
			LOG( "Wrong value of parameter " + paramName + " of material instance " + std::to_string( idInstance ) );
	}

	return true;
}

void LibraryLoader::save()
{	
	std::string insertedValue;
/*
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
	*/
}