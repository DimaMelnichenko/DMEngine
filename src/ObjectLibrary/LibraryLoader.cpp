#include "LibraryLoader.h"
#include "System.h"
#include "Materials\Material.h"
#include <iostream>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "DBConnector.h"
#include "Logger\Logger.h"

using namespace DirectX;




LibraryLoader::LibraryLoader()
{
}


LibraryLoader::~LibraryLoader()
{
}

bool LibraryLoader::loadAllTextures()
{
	LOG( "Load all textures" );
	SQLite::Statement queryTexture( DBConnector::instance().db(), "SELECT id, name, file, generate_mipmap, sRGB, preserve_alpha_coverage FROM Textures" );
	return loadTextures( queryTexture );
}

bool LibraryLoader::loadTexture( uint32_t idTexture )
{
	if( GS::System::textures().exists( idTexture ) )
		return true;

	SQLite::Statement queryTexture( DBConnector::instance().db(), "SELECT id, name, file, generate_mipmap, sRGB, preserve_alpha_coverage FROM Textures where id = :id" );
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
		
		const float preserveAlphaCoverage = queryTexture.getColumn( 5 ).isNull() ? 0.0f : static_cast<float>( queryTexture.getColumn( 5 ).getDouble() );
		if( !GS::System::textures().load( id, textureName, fileName, queryTexture.getColumn( 3 ).getInt(), queryTexture.getColumn( 4 ).getInt(),
										  preserveAlphaCoverage ) )
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
	SQLite::Statement query( DBConnector::instance().db(), "SELECT id FROM Materials ORDER BY id" );
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

	SQLite::Statement query( DBConnector::instance().db(), "SELECT id, name, class file FROM Materials where id = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{
		if( !GS::System::materials().createMaterial( query.getColumn( 0 ), query.getColumn( 1 ), query.getColumn( 2 ) ) )
			return false;
	}

	if( !GS::System::materials().exists( idMaterial ) )
		return false;
	
	GS::Material* material = GS::System::materials().get( idMaterial ).get();
	loadMaterialParamDef( idMaterial, material->parameters() );

	if( !loadShader( idMaterial, material ) )
		return false;

	if( !material->initialize() )
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
	SQLite::Statement query( DBConnector::instance().db(), "SELECT id_material, param_name, value_type, control_type, low, high, default_value FROM MaterialParameterDefView where id_material = :id" );
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

bool LibraryLoader::loadShader( uint32_t idMaterial, GS::Material* material )
{
	
	SQLite::Statement query( DBConnector::instance().db(), "SELECT material_id, file, type, define FROM MaterialShaderView where material_id = :id" );
	query.bind( ":id", idMaterial );
	while( query.executeStep() )
	{	
		std::string fullPath = GS::System::materials().path() + "\\" + query.getColumn( 1 ).getString();
		LOG( "Load shader: " + fullPath );
		// Shader.type — номер ShaderStageType (таблица ShaderType)
		const auto stage = static_cast<ShaderStageType>( query.getColumn( 2 ).getInt() );
		if( !material->addShaderPassFromFile( stage, "main", fullPath, query.getColumn( 3 ) ) )
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

	SQLite::Statement query( DBConnector::instance().db(), "SELECT id, name, file, primitive FROM Meshes where id = :id" );
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
		const char* columns = "SELECT id, name, terrain, sky, particles, atmosphere, post_process, sun_position, hdri_backdrop, wind FROM Levels ";
		SQLite::Statement query( DBConnector::instance().db(), std::string( columns ) + ( name.empty() ? "ORDER BY id LIMIT 1" : "WHERE name = :name" ) );
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
		if( !query.getColumn( "atmosphere" ).isNull() )
			level.atmosphereId = query.getColumn( "atmosphere" ).getUInt();
		if( !query.getColumn( "post_process" ).isNull() )
			level.postProcessId = query.getColumn( "post_process" ).getUInt();
		if( !query.getColumn( "sun_position" ).isNull() )
			level.sunPositionId = query.getColumn( "sun_position" ).getUInt();
		if( !query.getColumn( "hdri_backdrop" ).isNull() )
			level.hdriBackdropId = query.getColumn( "hdri_backdrop" ).getUInt();
		if( !query.getColumn( "wind" ).isNull() )
			level.windId = query.getColumn( "wind" ).getUInt();

		loadLevelLights( level );
		if( !loadLevelEnvironment( level ) )
			return false;

		SQLite::Statement queryModels( DBConnector::instance().db(), "SELECT id, model, position, rotation, scale FROM LevelModels "
																	 "WHERE level = :level ORDER BY id" );
		queryModels.bind( ":level", level.id );
		while( queryModels.executeStep() )
		{
			LevelDescription::ModelInstance instance;
			instance.model = queryModels.getColumn( "model" ).getUInt();
			const std::string row = "Level model " + queryModels.getColumn( "id" ).getString();
			const std::string position = queryModels.getColumn( "position" ).getString();
			const std::string rotation = queryModels.getColumn( "rotation" ).getString();
			const std::string scale = queryModels.getColumn( "scale" ).getString();
			if( !position.empty() && !strToVec3( position, instance.position ) )
				LOG( row + ": wrong position '" + position + "'" );
			if( !rotation.empty() && !strToVec4( rotation, instance.rotation ) )
			{
				LOG( row + ": wrong rotation '" + rotation + "', expected quaternion x,y,z,w" );
				instance.rotation = XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f );
			}
			if( !scale.empty() && !strToVec3( scale, instance.scale ) )
				LOG( row + ": wrong scale '" + scale + "'" );
			level.modelInstances.push_back( instance );
		}

		SQLite::Statement querySets( DBConnector::instance().db(), "SELECT s.id, s.name "
																   "FROM LevelScatterSets l JOIN ScatterSets s ON s.id = l.scatter_set "
																   "WHERE l.level = :level ORDER BY l.id" );
		querySets.bind( ":level", level.id );
		while( querySets.executeStep() )
		{
			LevelDescription::ScatterSet set;
			set.name = querySets.getColumn( "name" ).getString();
			loadScatterLayers( querySets.getColumn( "id" ).getUInt(), set );
			level.scatterSets.push_back( std::move( set ) );
		}

		if( hasParticles )
		{
			SQLite::Statement queryParticles( DBConnector::instance().db(), "SELECT material, texture, count_per_cell, area_size FROM Particles WHERE id = :id" );
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
	SQLite::Statement query( DBConnector::instance().db(), "SELECT id, mask, cell_size, near_border, far_border, near_fade, "
														   "far_fade, size_multiplier, jitter, rotation_x, rotation_y, rotation_z, align_to_terrain, cast_shadow, persistent "
														   "FROM ScatterLayers WHERE scatter_set = :set ORDER BY layer" );
	query.bind( ":set", idSet );
	while( query.executeStep() )
	{
		auto value = [&query]( const char* column )
		{
			return static_cast<float>( query.getColumn( column ).getDouble() );
		};

		LevelDescription::ScatterLayer layer;
		layer.mask = query.getColumn( "mask" ).getString();
		layer.settings.cellSize = value( "cell_size" );
		layer.settings.nearBorder = value( "near_border" );
		layer.settings.farBorder = value( "far_border" );
		layer.settings.nearFade = value( "near_fade" );
		layer.settings.farFade = value( "far_fade" );
		layer.settings.sizeMultiplier = value( "size_multiplier" );
		layer.settings.jitter = value( "jitter" );
		layer.settings.rotationRange = XMFLOAT3( value( "rotation_x" ), value( "rotation_y" ), value( "rotation_z" ) );
		layer.settings.alignToTerrain = query.getColumn( "align_to_terrain" ).getInt() != 0;
		layer.settings.castShadow = query.getColumn( "cast_shadow" ).getInt() != 0;
		layer.settings.persistent = query.getColumn( "persistent" ).getInt() != 0;

		// Модели слоя — варианты растения с весами, в порядке строк
		SQLite::Statement queryModels( DBConnector::instance().db(), "SELECT model, weight, cast_shadow FROM ScatterLayerModels WHERE layer = :layer ORDER BY id" );
		queryModels.bind( ":layer", query.getColumn( "id" ).getUInt() );
		while( queryModels.executeStep() )
		{
			LevelDescription::ScatterModel model;
			model.model = queryModels.getColumn( "model" ).getUInt();
			model.weight = static_cast<float>( queryModels.getColumn( "weight" ).getDouble() );
			model.castShadow = queryModels.getColumn( "cast_shadow" ).getInt() != 0;
			layer.models.push_back( model );
		}
		set.layers.push_back( layer );
	}
}

bool LibraryLoader::loadModelWithLOD( uint32_t idModel )
{
	if( GS::System::models().exists( idModel ) )
		return true;

	LOG( "Load model: " + std::to_string( idModel ) );

	SQLite::Statement query( DBConnector::instance().db(), "SELECT id, name FROM Models where id = :id" );
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
	
	// Строка — секция LOD (меш и материал); дальность и флаг отрисовки LOD — у его первой секции
	SQLite::Statement queryLOD( DBConnector::instance().db(), "SELECT id, lod, section, range, material_id, mesh_id, render, material_instance_id "
															  "FROM ModelProperties where model_id = :id order by lod, section" );
	queryLOD.bind( ":id", idModel );
	std::unique_ptr<GS::DMModel::LodBlock> block;
	int blockLod = -1;
	float blockRange = 0.0f;
	auto finishLod = [&]()
	{
		if( !block )
			return;
		for( size_t i = 0; i < block->sections.size(); ++i )
		{
			const DirectX::BoundingBox& meshBounds = GS::System::meshes().get( block->sections[i]->mesh )->bounds();
			if( i == 0 )
				block->bounds = meshBounds;
			else
				DirectX::BoundingBox::CreateMerged( block->bounds, block->bounds, meshBounds );
		}
		model->addLod( blockRange, std::move( block ) );
	};
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

		const int lod = queryLOD.getColumn( "lod" ).getInt();
		if( lod != blockLod )
		{
			finishLod();
			block = std::make_unique<GS::DMModel::LodBlock>();
			block->isRender = queryLOD.getColumn( "render" ).getInt();
			blockLod = lod;
			blockRange = static_cast<float>( queryLOD.getColumn( "range" ).getDouble() );
		}

		auto section = std::make_unique<GS::DMModel::Section>();
		section->material = idMaterial;
		section->mesh = queryLOD.getColumn( "mesh_id" );
		section->params = GS::System::materials().get( idParamsMaterial )->parameters();
		if( idInstance != 0 )
			loadMaterialParams( idInstance, section->params );
		model->properties()->addSubContainer( &section->params );
		block->sections.push_back( std::move( section ) );
	}
	finishLod();

	// Имена параметров в GUI: одинаковые имена в одном окне ImGui путаются, поэтому — номер LOD и секции
	for( uint16_t lod = 0; lod < model->lodCount(); ++lod )
	{
		const auto& sections = model->getLodById( lod )->sections;
		for( size_t i = 0; i < sections.size(); ++i )
		{
			const std::string material = GS::System::materials().get( sections[i]->material )->name();
			sections[i]->params.setName( "LOD " + std::to_string( lod ) +
										 ( sections.size() > 1 ? ", section " + std::to_string( i ) : std::string() ) +
										 " (" + material + ")" );
		}
	}

	return true;
}

bool LibraryLoader::instanceMaterial( uint32_t idInstance, uint32_t& idMaterial )
{
	SQLite::Statement query( DBConnector::instance().db(), "SELECT id_material FROM MaterialInstance where id_instance = :instance" );
	query.bind( ":instance", idInstance );
	if( !query.executeStep() || query.getColumn( 0 ).isNull() )
		return false;

	idMaterial = query.getColumn( 0 ).getInt();
	return true;
}

bool LibraryLoader::loadMaterialParams( uint32_t idInstance, PropertyContainer& paramSet )
{
	SQLite::Statement query( DBConnector::instance().db(), "SELECT id_instance, param_name, value FROM MaterialParamsValueView where id_instance = :instance" );
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
					SQLite::Statement query( DBConnector::instance().db(), "UPDATE MaterialParameterValue SET value = :value WHERE id = :id" );
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