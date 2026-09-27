#include "LibraryLoader.h"
#include "System.h"
#include <iostream>
#include <chrono>
#include <cstdio>
#include <cstdlib>

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
	SQLite::Statement queryTexture( dbConnect().db(), "SELECT id, name, file, generate_mipmap, sRGB, preserve_alpha_coverage FROM Textures" );
	return loadTextures( queryTexture );
}

bool LibraryLoader::loadTexture( uint32_t idTexture )
{
	if( GS::System::textures().exists( idTexture ) )
		return true;

	SQLite::Statement queryTexture( dbConnect().db(), "SELECT id, name, file, generate_mipmap, sRGB, preserve_alpha_coverage FROM Textures where id = :id" );
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
		const char* columns = "SELECT id, name, terrain, sky, particles, atmosphere, post_process, sun_position, hdri_backdrop FROM Levels ";
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
		if( !query.getColumn( "atmosphere" ).isNull() )
			level.atmosphereId = query.getColumn( "atmosphere" ).getUInt();
		if( !query.getColumn( "post_process" ).isNull() )
			level.postProcessId = query.getColumn( "post_process" ).getUInt();
		if( !query.getColumn( "sun_position" ).isNull() )
			level.sunPositionId = query.getColumn( "sun_position" ).getUInt();
		if( !query.getColumn( "hdri_backdrop" ).isNull() )
			level.hdriBackdropId = query.getColumn( "hdri_backdrop" ).getUInt();

		loadLevelLights( level );
		if( !loadLevelEnvironment( level ) )
			return false;

		SQLite::Statement queryModels( dbConnect().db(), "SELECT id, model, position, rotation, scale FROM LevelModels "
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

		SQLite::Statement querySets( dbConnect().db(), "SELECT s.id, s.name "
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

void LibraryLoader::loadLevelLights( LevelDescription& level )
{
	SQLite::Statement query( dbConnect().db(), "SELECT * FROM LevelLights WHERE level = :level ORDER BY id" );
	query.bind( ":level", level.id );
	while( query.executeStep() )
	{
		auto value = [&query]( const char* column )
		{
			return static_cast<float>( query.getColumn( column ).getDouble() );
		};
		auto vector = [&query]( const char* column, XMFLOAT3& result )
		{
			const std::string text = query.getColumn( column ).getString();
			if( !strToVec3( text, result ) )
				LOG( "Level light " + query.getColumn( "id" ).getString() + ": wrong " + column + " '" + text + "'" );
		};

		DMLight light( DMLight::strToType( query.getColumn( "type" ).getString() ) );
		light.id = query.getColumn( "id" ).getUInt();
		light.name = query.getColumn( "name" ).getString();
		light.setEnabled( query.getColumn( "enabled" ).getInt() != 0 );
		XMFLOAT3 vec( 1.0f, 1.0f, 1.0f );
		vector( "color", vec );
		light.setColor( vec );
		light.setIntensity( value( "intensity" ) );
		vec = XMFLOAT3( 0.0f, 0.0f, 0.0f );
		vector( "position", vec );
		light.setPosition( vec );
		vec = XMFLOAT3( 0.0f, -1.0f, 0.0f );
		vector( "direction", vec );
		light.setDirection( vec );
		light.setAttenuationRadius( value( "attenuation_radius" ) );
		light.setConeAngles( value( "inner_cone_angle" ), value( "outer_cone_angle" ) );

		DMLight::ShadowSettings shadows;
		shadows.castShadows = query.getColumn( "cast_shadows" ).getInt() != 0;
		shadows.dynamicShadowDistance = value( "dynamic_shadow_distance" );
		shadows.cascadeDistributionExponent = value( "cascade_distribution_exponent" );
		shadows.cascadeTransitionFraction = value( "cascade_transition_fraction" );
		shadows.shadowDistanceFadeoutFraction = value( "shadow_distance_fadeout_fraction" );
		shadows.shadowBias = value( "shadow_bias" );
		shadows.normalBias = value( "normal_bias" );
		shadows.shadowSlopeBias = value( "shadow_slope_bias" );
		light.setShadowSettings( shadows );
		light.setAtmosphereSunLight( query.getColumn( "atmosphere_sun_light" ).getInt() != 0 );

		level.lights.push_back( std::move( light ) );
	}
}

bool LibraryLoader::loadLevelEnvironment( LevelDescription& level )
{
	if( level.sunPositionId )
	{
		SQLite::Statement query( dbConnect().db(), "SELECT * FROM SunPosition WHERE id = :id" );
		query.bind( ":id", *level.sunPositionId );
		if( !query.executeStep() )
		{
			LOG( "Sun position " + std::to_string( *level.sunPositionId ) + " is not found in table SunPosition" );
			return false;
		}
		auto value = [&query]( const char* column )
		{
			return static_cast<float>( query.getColumn( column ).getDouble() );
		};
		SunPosition::Settings& settings = level.sunPosition.emplace();
		settings.latitude = value( "latitude" );
		settings.longitude = value( "longitude" );
		settings.timeZone = value( "time_zone" );
		settings.northOffset = value( "north_offset" );
		settings.timeOfDay = value( "time_of_day" );
		// Дата — как в SQLite: YYYY-MM-DD
		const std::string date = query.getColumn( "date" ).getString();
		if( sscanf_s( date.c_str(), "%d-%d-%d", &settings.year, &settings.month, &settings.day ) != 3 )
			LOG( "Sun position " + std::to_string( *level.sunPositionId ) + ": wrong date '" + date + "', expected YYYY-MM-DD" );
	}

	if( level.atmosphereId )
	{
		SQLite::Statement query( dbConnect().db(), "SELECT sky_intensity, haze, ground_albedo, aerial_perspective_view_distance_scale "
												   "FROM SkyAtmosphere WHERE id = :id" );
		query.bind( ":id", *level.atmosphereId );
		if( !query.executeStep() )
		{
			LOG( "Sky atmosphere " + std::to_string( *level.atmosphereId ) + " is not found in table SkyAtmosphere" );
			return false;
		}
		level.atmosphere.skyIntensity = static_cast<float>( query.getColumn( "sky_intensity" ).getDouble() );
		level.atmosphere.haze = static_cast<float>( query.getColumn( "haze" ).getDouble() );
		level.atmosphere.groundAlbedo = static_cast<float>( query.getColumn( "ground_albedo" ).getDouble() );
		level.atmosphere.aerialPerspectiveViewDistanceScale =
			static_cast<float>( query.getColumn( "aerial_perspective_view_distance_scale" ).getDouble() );
	}

	if( level.hdriBackdropId )
	{
		SQLite::Statement query( dbConnect().db(), "SELECT texture, intensity, rotation, max_luminance FROM HDRIBackdrop WHERE id = :id" );
		query.bind( ":id", *level.hdriBackdropId );
		if( !query.executeStep() )
		{
			LOG( "HDRI backdrop " + std::to_string( *level.hdriBackdropId ) + " is not found in table HDRIBackdrop" );
			return false;
		}
		GS::HDRIBackdrop::Settings& hdri = level.hdriBackdrop.emplace();
		hdri.texture = query.getColumn( "texture" ).getString();
		hdri.intensity = static_cast<float>( query.getColumn( "intensity" ).getDouble() );
		hdri.rotation = static_cast<float>( query.getColumn( "rotation" ).getDouble() );
		hdri.maxLuminance = static_cast<float>( query.getColumn( "max_luminance" ).getDouble() );
	}

	if( level.postProcessId )
	{
		SQLite::Statement query( dbConnect().db(), "SELECT * FROM PostProcessSettings WHERE id = :id" );
		query.bind( ":id", *level.postProcessId );
		if( !query.executeStep() )
		{
			LOG( "Post process settings " + std::to_string( *level.postProcessId ) + " are not found in table PostProcessSettings" );
			return false;
		}
		auto value = [&query]( const char* column )
		{
			return static_cast<float>( query.getColumn( column ).getDouble() );
		};
		GS::PostProcess::Settings& settings = level.postProcess;
		settings.meteringMode = GS::PostProcess::meteringModeFromName( query.getColumn( "metering_mode" ).getString() );
		settings.manualEV100 = value( "manual_ev100" );
		settings.minEV100 = value( "min_ev100" );
		settings.maxEV100 = value( "max_ev100" );
		settings.histogramLowPercent = value( "histogram_low_percent" );
		settings.histogramHighPercent = value( "histogram_high_percent" );
		settings.speedUp = value( "speed_up" );
		settings.speedDown = value( "speed_down" );
		level.postProcess.exposureCompensation = static_cast<float>( query.getColumn( "exposure_compensation" ).getDouble() );
		level.postProcess.tonemapper = GS::PostProcess::tonemapperFromName( query.getColumn( "tonemapper" ).getString() );
		level.postProcess.bloomIntensity = static_cast<float>( query.getColumn( "bloom_intensity" ).getDouble() );
		level.postProcess.bloomThreshold = static_cast<float>( query.getColumn( "bloom_threshold" ).getDouble() );
	}
	return true;
}

namespace
{

// Число для базы — с короткой записью (%g), как векторы: float 0.1 не превращается в 0.10000000149011612
double dbValue( float value )
{
	char text[32];
	std::snprintf( text, sizeof( text ), "%g", value );
	return std::atof( text );
}

}

bool LibraryLoader::saveLevelEnvironment( LevelDescription& level, const std::vector<DMLight>& lights,
										  const std::optional<SunPosition::Settings>& sunPosition,
										  const std::optional<GS::SkyAtmosphere::Settings>& atmosphere,
										  const std::optional<GS::HDRIBackdrop::Settings>& hdri,
										  const GS::PostProcess::Settings& postProcess )
{
	try
	{
		SQLite::Database& db = dbConnect().db();
		SQLite::Transaction transaction( db );

		SQLite::Statement updateLight( db, "UPDATE LevelLights SET name = :name, enabled = :enabled, color = :color, "
										   "intensity = :intensity, position = :position, direction = :direction, "
										   "attenuation_radius = :radius, inner_cone_angle = :inner, outer_cone_angle = :outer, "
										   "cast_shadows = :castShadows, dynamic_shadow_distance = :distance, "
										   "cascade_distribution_exponent = :exponent, cascade_transition_fraction = :transition, "
										   "shadow_distance_fadeout_fraction = :fadeout, shadow_bias = :shadowBias, "
										   "normal_bias = :normalBias, shadow_slope_bias = :slopeBias, "
										   "atmosphere_sun_light = :atmosphereSunLight WHERE id = :id" );
		for( const DMLight& light : lights )
		{
			if( light.id == 0 )
				continue;
			const DMLight::ShadowSettings& shadows = light.shadowSettings();
			updateLight.bind( ":name", light.name );
			updateLight.bind( ":enabled", light.enabled() ? 1 : 0 );
			updateLight.bind( ":color", vec3ToStr( light.color() ) );
			updateLight.bind( ":intensity", dbValue( light.intensity() ) );
			updateLight.bind( ":position", vec3ToStr( light.position() ) );
			updateLight.bind( ":direction", vec3ToStr( light.direction() ) );
			updateLight.bind( ":radius", dbValue( light.attenuationRadius() ) );
			updateLight.bind( ":inner", dbValue( light.innerConeAngle() ) );
			updateLight.bind( ":outer", dbValue( light.outerConeAngle() ) );
			updateLight.bind( ":castShadows", shadows.castShadows ? 1 : 0 );
			updateLight.bind( ":distance", dbValue( shadows.dynamicShadowDistance ) );
			updateLight.bind( ":exponent", dbValue( shadows.cascadeDistributionExponent ) );
			updateLight.bind( ":transition", dbValue( shadows.cascadeTransitionFraction ) );
			updateLight.bind( ":fadeout", dbValue( shadows.shadowDistanceFadeoutFraction ) );
			updateLight.bind( ":shadowBias", dbValue( shadows.shadowBias ) );
			updateLight.bind( ":normalBias", dbValue( shadows.normalBias ) );
			updateLight.bind( ":slopeBias", dbValue( shadows.shadowSlopeBias ) );
			updateLight.bind( ":atmosphereSunLight", light.atmosphereSunLight() ? 1 : 0 );
			updateLight.bind( ":id", light.id );
			updateLight.exec();
			updateLight.reset();
		}

		// Место и время — только если строка у уровня есть: без неё солнце светит по своему направлению
		if( sunPosition && level.sunPositionId )
		{
			char date[16];
			std::snprintf( date, sizeof( date ), "%04d-%02d-%02d", sunPosition->year, sunPosition->month, sunPosition->day );
			SQLite::Statement updateSun( db, "UPDATE SunPosition SET latitude = :latitude, longitude = :longitude, "
											 "time_zone = :timeZone, north_offset = :northOffset, date = :date, "
											 "time_of_day = :timeOfDay WHERE id = :id" );
			updateSun.bind( ":latitude", dbValue( sunPosition->latitude ) );
			updateSun.bind( ":longitude", dbValue( sunPosition->longitude ) );
			updateSun.bind( ":timeZone", dbValue( sunPosition->timeZone ) );
			updateSun.bind( ":northOffset", dbValue( sunPosition->northOffset ) );
			updateSun.bind( ":date", std::string( date ) );
			updateSun.bind( ":timeOfDay", dbValue( sunPosition->timeOfDay ) );
			updateSun.bind( ":id", *level.sunPositionId );
			updateSun.exec();
		}

		// Строки неба и постобработки: нет у уровня — создаются с его именем. Небо — то, что у уровня работает:
		// атмосфера или панорама (строку панорамы задают в базе, без неё небо — атмосфера)
		if( atmosphere )
		{
			if( !level.atmosphereId )
			{
				SQLite::Statement insert( db, "INSERT INTO SkyAtmosphere (name) VALUES (:name)" );
				insert.bind( ":name", level.name );
				insert.exec();
				level.atmosphereId = static_cast<uint32_t>( db.getLastInsertRowid() );
			}
			SQLite::Statement updateAtmosphere( db, "UPDATE SkyAtmosphere SET sky_intensity = :intensity, haze = :haze, "
													"ground_albedo = :albedo, aerial_perspective_view_distance_scale = :aerialScale "
													"WHERE id = :id" );
			updateAtmosphere.bind( ":intensity", dbValue( atmosphere->skyIntensity ) );
			updateAtmosphere.bind( ":haze", dbValue( atmosphere->haze ) );
			updateAtmosphere.bind( ":albedo", dbValue( atmosphere->groundAlbedo ) );
			updateAtmosphere.bind( ":aerialScale", dbValue( atmosphere->aerialPerspectiveViewDistanceScale ) );
			updateAtmosphere.bind( ":id", *level.atmosphereId );
			updateAtmosphere.exec();

			SQLite::Statement updateLevel( db, "UPDATE Levels SET atmosphere = :atmosphere WHERE id = :id" );
			updateLevel.bind( ":atmosphere", *level.atmosphereId );
			updateLevel.bind( ":id", level.id );
			updateLevel.exec();
		}
		if( hdri && level.hdriBackdropId )
		{
			SQLite::Statement updateHDRI( db, "UPDATE HDRIBackdrop SET intensity = :intensity, rotation = :rotation, "
											  "max_luminance = :maxLuminance WHERE id = :id" );
			updateHDRI.bind( ":intensity", dbValue( hdri->intensity ) );
			updateHDRI.bind( ":rotation", dbValue( hdri->rotation ) );
			updateHDRI.bind( ":maxLuminance", dbValue( hdri->maxLuminance ) );
			updateHDRI.bind( ":id", *level.hdriBackdropId );
			updateHDRI.exec();
		}

		if( !level.postProcessId )
		{
			SQLite::Statement insert( db, "INSERT INTO PostProcessSettings (name) VALUES (:name)" );
			insert.bind( ":name", level.name );
			insert.exec();
			level.postProcessId = static_cast<uint32_t>( db.getLastInsertRowid() );
		}
		SQLite::Statement updatePostProcess( db, "UPDATE PostProcessSettings SET exposure_compensation = :exposure, "
												 "tonemapper = :tonemapper, bloom_intensity = :bloomIntensity, "
												 "bloom_threshold = :bloomThreshold, metering_mode = :meteringMode, "
												 "manual_ev100 = :manualEV100, min_ev100 = :minEV100, max_ev100 = :maxEV100, "
												 "histogram_low_percent = :lowPercent, histogram_high_percent = :highPercent, "
												 "speed_up = :speedUp, speed_down = :speedDown WHERE id = :id" );
		updatePostProcess.bind( ":exposure", dbValue( postProcess.exposureCompensation ) );
		updatePostProcess.bind( ":tonemapper", GS::PostProcess::tonemapperName( postProcess.tonemapper ) );
		updatePostProcess.bind( ":bloomIntensity", dbValue( postProcess.bloomIntensity ) );
		updatePostProcess.bind( ":bloomThreshold", dbValue( postProcess.bloomThreshold ) );
		updatePostProcess.bind( ":meteringMode", GS::PostProcess::meteringModeName( postProcess.meteringMode ) );
		updatePostProcess.bind( ":manualEV100", dbValue( postProcess.manualEV100 ) );
		updatePostProcess.bind( ":minEV100", dbValue( postProcess.minEV100 ) );
		updatePostProcess.bind( ":maxEV100", dbValue( postProcess.maxEV100 ) );
		updatePostProcess.bind( ":lowPercent", dbValue( postProcess.histogramLowPercent ) );
		updatePostProcess.bind( ":highPercent", dbValue( postProcess.histogramHighPercent ) );
		updatePostProcess.bind( ":speedUp", dbValue( postProcess.speedUp ) );
		updatePostProcess.bind( ":speedDown", dbValue( postProcess.speedDown ) );
		updatePostProcess.bind( ":id", *level.postProcessId );
		updatePostProcess.exec();

		SQLite::Statement updateLevel( db, "UPDATE Levels SET post_process = :postProcess WHERE id = :id" );
		updateLevel.bind( ":postProcess", *level.postProcessId );
		updateLevel.bind( ":id", level.id );
		updateLevel.exec();

		transaction.commit();
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Can`t save level environment: " ) + e.what() );
		return false;
	}

	return true;
}

void LibraryLoader::loadScatterLayers( uint32_t idSet, LevelDescription::ScatterSet& set )
{
	SQLite::Statement query( dbConnect().db(), "SELECT id, mask, cell_size, near_border, far_border, near_fade, "
											   "far_fade, size_multiplier, jitter, rotation_x, rotation_y, rotation_z, align_to_terrain, cast_shadow "
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
		layer.params.cellSize = value( "cell_size" );
		layer.params.nearBorder = value( "near_border" );
		layer.params.farBorder = value( "far_border" );
		layer.params.nearFade = value( "near_fade" );
		layer.params.farFade = value( "far_fade" );
		layer.params.sizeMultiplier = value( "size_multiplier" );
		layer.params.jitter = value( "jitter" );
		layer.params.rotationRange = XMFLOAT3( XMConvertToRadians( value( "rotation_x" ) ), XMConvertToRadians( value( "rotation_y" ) ),
											   XMConvertToRadians( value( "rotation_z" ) ) );
		layer.params.alignToTerrain = query.getColumn( "align_to_terrain" ).getInt() ? 1.0f : 0.0f;
		layer.params.castShadow = query.getColumn( "cast_shadow" ).getInt() ? 1.0f : 0.0f;

		// Модели слоя — варианты растения с весами, в порядке строк
		SQLite::Statement queryModels( dbConnect().db(), "SELECT model, weight, cast_shadow FROM ScatterLayerModels WHERE layer = :layer ORDER BY id" );
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