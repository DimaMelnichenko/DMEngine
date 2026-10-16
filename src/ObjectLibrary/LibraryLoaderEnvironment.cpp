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

namespace
{

// Прежнее значение необязательной строки (снимок без неё — прежнего нет)
template<typename T>
const T* previousOf( const std::optional<T>& previous )
{
	return previous ? &*previous : nullptr;
}

// Строка окружения уровня, которой у него может не быть (постобработка, ветер, туман, атмосфера вместо панорамы): есть
// что писать, а строки нет — создаётся с именем уровня и привязывается к Levels.<levelColumn> или, при активном пресете,
// к той же колонке LevelPresets
void updateLevelRow( SQLite::Database& db, const RowUpdate& update, const char* table, const char* levelColumn,
					 LevelDescription& level, std::optional<uint32_t>& rowId, std::optional<uint32_t>& levelRow )
{
	if( update.empty() )
		return;
	if( !rowId )
	{
		SQLite::Statement insert( db, std::string( "INSERT INTO " ) + table + " (name) VALUES (:name)" );
		insert.bind( ":name", level.preset.empty() ? level.name : level.name + " " + level.preset );
		insert.exec();
		rowId = static_cast<uint32_t>( db.getLastInsertRowid() );
		if( level.preset.empty() )
		{
			SQLite::Statement link( db, std::string( "UPDATE Levels SET " ) + levelColumn + " = :row WHERE id = :id" );
			link.bind( ":row", *rowId );
			link.bind( ":id", level.id );
			link.exec();
			levelRow = rowId;
		}
		else
		{
			SQLite::Statement link( db, std::string( "UPDATE LevelPresets SET " ) + levelColumn + " = :row WHERE level = :level AND name = :name" );
			link.bind( ":row", *rowId );
			link.bind( ":level", level.id );
			link.bind( ":name", level.preset );
			link.exec();
		}
	}
	update.update( db, table, { { "id", dbInt( *rowId ) } } );
}

DbValue vectorValue( const XMFLOAT3& value )
{
	return vec3ToStr( value );
}

}

bool LibraryLoader::saveLevelEnvironment( LevelDescription& level, LevelSaveData& data, const LevelSaveData& previous )
{
	try
	{
		SQLite::Database& db = DBConnector::instance().db();
		SQLite::Transaction transaction( db );

		// Убранные в редакторе источники — строки удаляются; добавленные — новые строки уровня (тип и номер светила
		// атмосферы в GUI не меняются, остальное пишет UPDATE ниже — у новой строки все колонки). id новых строк — в
		// источники: после сохранения они правятся по id, как загруженные
		SQLite::Statement deleteLight( db, "DELETE FROM LevelLights WHERE id = :id" );
		for( uint32_t id : data.removedLights )
		{
			deleteLight.bind( ":id", id );
			deleteLight.exec();
			deleteLight.reset();
		}
		SQLite::Statement insertLight( db, "INSERT INTO LevelLights (level, type, atmosphere_sun_light_index) VALUES (:level, :type, :index)" );
		std::vector<bool> inserted( data.lights.size(), false );
		for( size_t i = 0; i < data.lights.size(); ++i )
		{
			DMLight& light = data.lights[i];
			if( light.id != 0 )
				continue;
			insertLight.bind( ":level", level.id );
			insertLight.bind( ":type", std::string( DMLight::typeName( light.type() ) ) );
			insertLight.bind( ":index", light.atmosphereSunLightIndex() );
			insertLight.exec();
			insertLight.reset();
			light.id = static_cast<uint32_t>( db.getLastInsertRowid() );
			inserted[i] = true;
		}

		for( size_t i = 0; i < data.lights.size(); ++i )
		{
			const DMLight& light = data.lights[i];
			// Прежнее — по id; у вставленной сейчас строки его нет (id мог достаться от удалённой)
			const DMLight* before = inserted[i] ? nullptr : findPrevious( previous.lights, light.id, []( const DMLight& l ) { return l.id; } );
			RowUpdate update;
			update.set( "name", light, before, []( const DMLight& l ) { return DbValue( l.name ); } );
			update.set( "enabled", light, before, []( const DMLight& l ) { return dbInt( l.enabled() ? 1 : 0 ); } );
			update.set( "color", light, before, []( const DMLight& l ) { return vectorValue( l.color() ); } );
			// Что считает SunPosition (направление светил, освещённость луны по фазе), у строки в базе не пишется
			if( !before || i >= data.intensityFromTime.size() || !data.intensityFromTime[i] )
				update.set( "intensity", light, before, []( const DMLight& l ) { return dbFloat( l.intensity() ); } );
			update.set( "position", light, before, []( const DMLight& l ) { return vectorValue( l.position() ); } );
			if( !before || i >= data.directionFromTime.size() || !data.directionFromTime[i] )
				update.set( "direction", light, before, []( const DMLight& l ) { return vectorValue( l.direction() ); } );
			update.set( "attenuation_radius", light, before, []( const DMLight& l ) { return dbFloat( l.attenuationRadius() ); } );
			update.set( "inner_cone_angle", light, before, []( const DMLight& l ) { return dbFloat( l.innerConeAngle() ); } );
			update.set( "outer_cone_angle", light, before, []( const DMLight& l ) { return dbFloat( l.outerConeAngle() ); } );
			update.settings( DMLight::shadowSchema(), light.shadowSettings(), before ? &before->shadowSettings() : nullptr );
			update.set( "atmosphere_sun_light", light, before, []( const DMLight& l ) { return dbInt( l.atmosphereSunLight() ? 1 : 0 ); } );
			update.update( db, "LevelLights", { { "id", dbInt( light.id ) } } );
		}

		// Панорама — не часть пресета: её строку задают в базе
		if( data.hdri && level.hdriBackdropId )
			updateSettings( db, "HDRIBackdrop", *level.hdriBackdropId, GS::hdriBackdropSchema(), *data.hdri, previousOf( previous.hdri ) );
		// Окружение — в строки активного пресета; у заблокированного (эталон проверок) — не пишется
		if( level.presetLocked )
		{
			LOG( "Preset '" + level.preset + "' is locked: environment is not saved (File > Preset > Save preset as)" );
		}
		else
			saveEnvironmentRows( db, level, data, previous );

		transaction.commit();
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Can`t save level environment: " ) + e.what() );
		return false;
	}

	return true;
}

void LibraryLoader::saveEnvironmentRows( SQLite::Database& db, LevelDescription& level, const LevelSaveData& data,
										 const LevelSaveData& previous )
{
	LevelDescription::EnvironmentRows& levelRows = level.levelRows;

	// Место и время — только если строка у уровня есть: без неё солнце светит по своему направлению
	if( data.sunPosition && level.sunPositionId )
	{
		const auto date = []( const GS::SunPositionSettings& sun )
		{
			char text[16];
			std::snprintf( text, sizeof( text ), "%04d-%02d-%02d", sun.year, sun.month, sun.day );
			return DbValue( std::string( text ) );
		};
		const GS::SunPositionSettings* before = previousOf( previous.sunPosition );
		RowUpdate update;
		update.settings( GS::sunPositionSchema(), *data.sunPosition, before );
		update.set( "date", *data.sunPosition, before, date );
		update.update( db, "SunPosition", { { "id", dbInt( *level.sunPositionId ) } } );
	}

	// Небо — то, что у уровня работает: атмосфера или панорама (строку панорамы задают в базе, без неё небо —
	// атмосфера). Атмосфера вместо незагруженной панорамы получает строку, только если её правили
	if( data.atmosphere )
	{
		RowUpdate update;
		update.settings( GS::skyAtmosphereSchema(), *data.atmosphere, previousOf( previous.atmosphere ) );
		updateLevelRow( db, update, "SkyAtmosphere", "atmosphere", level, level.atmosphereId, levelRows.atmosphere );
	}

	// Постобработка: поля описания, перечисления — именами, кривая компенсации — текстом (пустая — NULL)
	{
		const GS::PostProcessSettings& postProcess = data.postProcess;
		const GS::PostProcessSettings* before = &previous.postProcess;
		RowUpdate update;
		update.settings( GS::postProcessSchema(), postProcess, before );
		update.set( "tonemapper", postProcess, before,
					[]( const GS::PostProcessSettings& p ) { return DbValue( std::string( GS::tonemapperName( p.tonemapper ) ) ); } );
		update.set( "metering_mode", postProcess, before,
					[]( const GS::PostProcessSettings& p ) { return DbValue( std::string( GS::meteringModeName( p.meteringMode ) ) ); } );
		update.set( "exposure_compensation_curve", postProcess, before, []( const GS::PostProcessSettings& p )
		{
			return p.exposureCompensationCurve.empty() ? DbValue( nullptr ) : DbValue( GS::curveText( p.exposureCompensationCurve ) );
		} );
		updateLevelRow( db, update, "PostProcessSettings", "post_process", level, level.postProcessId, levelRows.postProcess );
	}

	// Ветер и поле ветра (колонки field_*) — по описаниям, направление — вектором (в окне — Yaw)
	{
		const GS::WindSettings& wind = data.wind;
		const GS::WindSettings* before = &previous.wind;
		RowUpdate update;
		update.settings( GS::windSchema(), wind, before );
		update.settings( GS::windFieldSchema(), wind.field, &before->field );
		update.set( "direction", wind, before, []( const GS::WindSettings& w ) { return vectorValue( w.direction ); } );
		updateLevelRow( db, update, "Wind", "wind", level, level.windId, levelRows.wind );
	}

	// Туман: строка создаётся, только если его включили в GUI; выключенный остаётся строкой с нулевой плотностью
	if( data.heightFog )
	{
		RowUpdate update;
		update.settings( GS::heightFogSchema(), *data.heightFog, previousOf( previous.heightFog ) );
		updateLevelRow( db, update, "ExponentialHeightFog", "height_fog", level, level.heightFogId, levelRows.heightFog );
	}

	// Облака — только в свою строку: без строки у уровня облаков нет, и окна тоже
	if( data.cloud && level.volumetricCloudId )
		updateSettings( db, "VolumetricCloud", *level.volumetricCloudId, GS::volumetricCloudSchema(), *data.cloud,
						previousOf( previous.cloud ) );
}
