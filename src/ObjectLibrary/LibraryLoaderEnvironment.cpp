#include "LibraryLoader.h"
#include <cstdio>
#include <cstdlib>
#include "DBConnector.h"
#include "Logger\Logger.h"
#include "Utils\utilites.h"
#include "SettingsDatabase.h"

using namespace DirectX;

// Свет и окружение уровня: строки LevelLights, SunPosition, SkyAtmosphere, Wind, HDRIBackdrop, PostProcessSettings,
// ExponentialHeightFog, VolumetricCloud и звёзды неба (Stars) —
// загрузка в LevelDescription и сохранение правок из GUI («Save level»)

void LibraryLoader::loadLevelLights( LevelDescription& level )
{
	SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM LevelLights WHERE level = :level ORDER BY id" );
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
		readSettings( query, DMLight::shadowSchema(), shadows, "Level light " + query.getColumn( "id" ).getString() );
		light.setShadowSettings( shadows );
		light.setAtmosphereSunLight( query.getColumn( "atmosphere_sun_light" ).getInt() != 0 );
		light.setAtmosphereSunLightIndex( query.getColumn( "atmosphere_sun_light_index" ).getInt() );

		level.lights.push_back( std::move( light ) );
	}
}

bool LibraryLoader::loadLevelEnvironment( LevelDescription& level )
{
	if( level.sunPositionId )
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM SunPosition WHERE id = :id" );
		query.bind( ":id", *level.sunPositionId );
		if( !query.executeStep() )
		{
			LOG( "Sun position " + std::to_string( *level.sunPositionId ) + " is not found in table SunPosition" );
			return false;
		}
		GS::SunPositionSettings& settings = level.sunPosition.emplace();
		readSettings( query, GS::sunPositionSchema(), settings, "Sun position " + std::to_string( *level.sunPositionId ) );
		// Дата — как в SQLite: YYYY-MM-DD
		const std::string date = query.getColumn( "date" ).getString();
		if( sscanf_s( date.c_str(), "%d-%d-%d", &settings.year, &settings.month, &settings.day ) != 3 )
			LOG( "Sun position " + std::to_string( *level.sunPositionId ) + ": wrong date '" + date + "', expected YYYY-MM-DD" );
	}

	if( level.atmosphereId )
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM SkyAtmosphere WHERE id = :id" );
		query.bind( ":id", *level.atmosphereId );
		if( !query.executeStep() )
		{
			LOG( "Sky atmosphere " + std::to_string( *level.atmosphereId ) + " is not found in table SkyAtmosphere" );
			return false;
		}
		readSettings( query, GS::skyAtmosphereSchema(), level.atmosphere, "Sky atmosphere " + std::to_string( *level.atmosphereId ) );
	}

	if( level.windId )
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM Wind WHERE id = :id" );
		query.bind( ":id", *level.windId );
		if( !query.executeStep() )
		{
			LOG( "Wind " + std::to_string( *level.windId ) + " is not found in table Wind" );
			return false;
		}
		GS::WindSettings& wind = level.wind.emplace();
		const std::string direction = query.getColumn( "direction" ).getString();
		if( !strToVec3( direction, wind.direction ) )
			LOG( "Wind " + std::to_string( *level.windId ) + ": wrong direction '" + direction + "'" );
		const std::string what = "Wind " + std::to_string( *level.windId );
		readSettings( query, GS::windSchema(), wind, what );
		// Поле ветра по рельефу — колонки field_*: нет колонки или NULL — значение по умолчанию
		readSettings( query, GS::windFieldSchema(), wind.field, what );
	}

	if( level.heightFogId )
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM ExponentialHeightFog WHERE id = :id" );
		query.bind( ":id", *level.heightFogId );
		if( !query.executeStep() )
		{
			LOG( "Height fog " + std::to_string( *level.heightFogId ) + " is not found in table ExponentialHeightFog" );
			return false;
		}
		readSettings( query, GS::heightFogSchema(), level.heightFog.emplace(), "Height fog " + std::to_string( *level.heightFogId ) );
	}

	if( level.volumetricCloudId )
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM VolumetricCloud WHERE id = :id" );
		query.bind( ":id", *level.volumetricCloudId );
		if( !query.executeStep() )
		{
			LOG( "Volumetric cloud " + std::to_string( *level.volumetricCloudId ) + " is not found in table VolumetricCloud" );
			return false;
		}
		readSettings( query, GS::volumetricCloudSchema(), level.volumetricCloud.emplace(),
					  "Volumetric cloud " + std::to_string( *level.volumetricCloudId ) );
	}

	if( level.hdriBackdropId )
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM HDRIBackdrop WHERE id = :id" );
		query.bind( ":id", *level.hdriBackdropId );
		if( !query.executeStep() )
		{
			LOG( "HDRI backdrop " + std::to_string( *level.hdriBackdropId ) + " is not found in table HDRIBackdrop" );
			return false;
		}
		readSettings( query, GS::hdriBackdropSchema(), level.hdriBackdrop.emplace(), "HDRI backdrop " + std::to_string( *level.hdriBackdropId ) );
	}

	if( level.postProcessId )
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM PostProcessSettings WHERE id = :id" );
		query.bind( ":id", *level.postProcessId );
		if( !query.executeStep() )
		{
			LOG( "Post process settings " + std::to_string( *level.postProcessId ) + " are not found in table PostProcessSettings" );
			return false;
		}
		GS::PostProcessSettings& settings = level.postProcess;
		readSettings( query, GS::postProcessSchema(), settings, "Post process settings " + std::to_string( *level.postProcessId ) );
		// Перечисления — именами, кривая — текстом
		settings.meteringMode = GS::meteringModeFromName( query.getColumn( "metering_mode" ).getString() );
		settings.tonemapper = GS::tonemapperFromName( query.getColumn( "tonemapper" ).getString() );
		const SQLite::Column curve = query.getColumn( "exposure_compensation_curve" );
		if( !curve.isNull() )
			level.postProcess.exposureCompensationCurve = GS::curveFromText( curve.getString() );
	}
	return true;
}

void LibraryLoader::loadStars( LevelDescription& level )
{
	try
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT ra, dec, magnitude, color_index FROM Stars ORDER BY hr" );
		while( query.executeStep() )
		{
			GS::StarRecord star;
			star.rightAscension = static_cast<float>( query.getColumn( "ra" ).getDouble() );
			star.declination = static_cast<float>( query.getColumn( "dec" ).getDouble() );
			star.magnitude = static_cast<float>( query.getColumn( "magnitude" ).getDouble() );
			star.colorIndex = static_cast<float>( query.getColumn( "color_index" ).getDouble() );
			level.stars.push_back( star );
		}
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "No stars in the sky (table Stars, Tools/star_catalog.py): " ) + e.what() );
		level.stars.clear();
	}
}

double LibraryLoader::dbValue( float value )
{
	return settingsDatabaseFloat( value );
}

bool LibraryLoader::saveLevelEnvironment( LevelDescription& level, std::vector<DMLight>& lights, const std::vector<uint32_t>& removedLights,
										  const std::optional<GS::SunPositionSettings>& sunPosition,
										  const std::optional<GS::SkyAtmosphereSettings>& atmosphere,
										  const std::optional<GS::HDRIBackdropSettings>& hdri,
										  const GS::PostProcessSettings& postProcess, const GS::WindSettings& wind,
										  const std::optional<GS::HeightFogSettings>& heightFog,
										  const std::optional<GS::VolumetricCloudSettings>& cloud )
{
	try
	{
		SQLite::Database& db = DBConnector::instance().db();
		SQLite::Transaction transaction( db );

		// Убранные в редакторе источники — строки удаляются; добавленные — новые строки уровня (тип и номер светила
		// атмосферы в GUI не меняются, остальное пишет UPDATE ниже). id новых строк — в источники: после сохранения
		// они правятся по id, как загруженные
		SQLite::Statement deleteLight( db, "DELETE FROM LevelLights WHERE id = :id" );
		for( uint32_t id : removedLights )
		{
			deleteLight.bind( ":id", id );
			deleteLight.exec();
			deleteLight.reset();
		}
		SQLite::Statement insertLight( db, "INSERT INTO LevelLights (level, type, atmosphere_sun_light_index) VALUES (:level, :type, :index)" );
		for( DMLight& light : lights )
		{
			if( light.id != 0 )
				continue;
			insertLight.bind( ":level", level.id );
			insertLight.bind( ":type", std::string( DMLight::typeName( light.type() ) ) );
			insertLight.bind( ":index", light.atmosphereSunLightIndex() );
			insertLight.exec();
			insertLight.reset();
			light.id = static_cast<uint32_t>( db.getLastInsertRowid() );
		}

		SQLite::Statement updateLight( db, "UPDATE LevelLights SET name = :name, enabled = :enabled, color = :color, "
										   "intensity = :intensity, position = :position, direction = :direction, "
										   "attenuation_radius = :radius, inner_cone_angle = :inner, outer_cone_angle = :outer, " +
										   settingsAssignments( DMLight::shadowSchema() ) +
										   ", atmosphere_sun_light = :atmosphereSunLight WHERE id = :id" );
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
			bindSettings( updateLight, DMLight::shadowSchema(), shadows );
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
			SQLite::Statement updateSun( db, "UPDATE SunPosition SET " + settingsAssignments( GS::sunPositionSchema() ) +
											 ", date = :date WHERE id = :id" );
			bindSettings( updateSun, GS::sunPositionSchema(), *sunPosition );
			updateSun.bind( ":date", std::string( date ) );
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
			updateSettings( db, "SkyAtmosphere", *level.atmosphereId, GS::skyAtmosphereSchema(), *atmosphere );

			SQLite::Statement updateLevel( db, "UPDATE Levels SET atmosphere = :atmosphere WHERE id = :id" );
			updateLevel.bind( ":atmosphere", *level.atmosphereId );
			updateLevel.bind( ":id", level.id );
			updateLevel.exec();
		}
		if( hdri && level.hdriBackdropId )
		{
			updateSettings( db, "HDRIBackdrop", *level.hdriBackdropId, GS::hdriBackdropSchema(), *hdri );
		}

		if( !level.postProcessId )
		{
			SQLite::Statement insert( db, "INSERT INTO PostProcessSettings (name) VALUES (:name)" );
			insert.bind( ":name", level.name );
			insert.exec();
			level.postProcessId = static_cast<uint32_t>( db.getLastInsertRowid() );
		}
		SQLite::Statement updatePostProcess( db, "UPDATE PostProcessSettings SET " + settingsAssignments( GS::postProcessSchema() ) +
												 ", tonemapper = :tonemapper, metering_mode = :meteringMode, "
												 "exposure_compensation_curve = :curve WHERE id = :id" );
		bindSettings( updatePostProcess, GS::postProcessSchema(), postProcess );
		updatePostProcess.bind( ":tonemapper", GS::tonemapperName( postProcess.tonemapper ) );
		updatePostProcess.bind( ":meteringMode", GS::meteringModeName( postProcess.meteringMode ) );
		if( postProcess.exposureCompensationCurve.empty() )
			updatePostProcess.bind( ":curve" );
		else
			updatePostProcess.bind( ":curve", GS::curveText( postProcess.exposureCompensationCurve ) );
		updatePostProcess.bind( ":id", *level.postProcessId );
		updatePostProcess.exec();

		SQLite::Statement updateLevel( db, "UPDATE Levels SET post_process = :postProcess WHERE id = :id" );
		updateLevel.bind( ":postProcess", *level.postProcessId );
		updateLevel.bind( ":id", level.id );
		updateLevel.exec();

		// Ветер: нет строки — создаётся и привязывается к уровню, как у постобработки
		if( !level.windId )
		{
			SQLite::Statement insert( db, "INSERT INTO Wind (name) VALUES (:name)" );
			insert.bind( ":name", level.name );
			insert.exec();
			level.windId = static_cast<uint32_t>( db.getLastInsertRowid() );
		}
		// Ветер и поле ветра (колонки field_*) — по описаниям, направление — вектором (в окне — Yaw)
		SQLite::Statement updateWind( db, "UPDATE Wind SET " + settingsAssignments( GS::windSchema() ) + ", " +
										  settingsAssignments( GS::windFieldSchema() ) + ", direction = :direction WHERE id = :id" );
		bindSettings( updateWind, GS::windSchema(), wind );
		bindSettings( updateWind, GS::windFieldSchema(), wind.field );
		updateWind.bind( ":direction", vec3ToStr( wind.direction ) );
		updateWind.bind( ":id", *level.windId );
		updateWind.exec();
		SQLite::Statement updateLevelWind( db, "UPDATE Levels SET wind = :wind WHERE id = :id" );
		updateLevelWind.bind( ":wind", *level.windId );
		updateLevelWind.bind( ":id", level.id );
		updateLevelWind.exec();

		// Туман: строка создаётся, только если его включили в GUI; выключенный остаётся строкой с нулевой плотностью
		if( heightFog )
		{
			if( !level.heightFogId )
			{
				SQLite::Statement insert( db, "INSERT INTO ExponentialHeightFog (name) VALUES (:name)" );
				insert.bind( ":name", level.name );
				insert.exec();
				level.heightFogId = static_cast<uint32_t>( db.getLastInsertRowid() );
			}
			updateSettings( db, "ExponentialHeightFog", *level.heightFogId, GS::heightFogSchema(), *heightFog );
			SQLite::Statement updateLevelFog( db, "UPDATE Levels SET height_fog = :fog WHERE id = :id" );
			updateLevelFog.bind( ":fog", *level.heightFogId );
			updateLevelFog.bind( ":id", level.id );
			updateLevelFog.exec();
		}

		// Облака — только в свою строку: без строки у уровня облаков нет, и окна тоже
		if( cloud && level.volumetricCloudId )
		{
			updateSettings( db, "VolumetricCloud", *level.volumetricCloudId, GS::volumetricCloudSchema(), *cloud );
		}

		transaction.commit();
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Can`t save level environment: " ) + e.what() );
		return false;
	}

	return true;
}
