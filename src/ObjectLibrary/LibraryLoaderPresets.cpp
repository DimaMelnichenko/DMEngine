#include "LibraryLoader.h"
#include <algorithm>
#include "DBConnector.h"
#include "Logger\Logger.h"
#include "SettingsDatabase.h"

// Пресеты окружения уровня (таблица LevelPresets): именованные наборы строк окружения — время суток, небо, постобработка,
// ветер, туман, облака (колонки — как у Levels: sun_position, atmosphere, post_process, wind, height_fog, volumetric_cloud;
// NULL — строка самого уровня), locked — «Save level» их не пишет (эталон проверок, Tools/check_frames.py)

namespace
{

// Колонка строки окружения и её id у уровня
struct EnvironmentColumn
{
	const char* table;
	const char* column;
	std::optional<uint32_t> LevelDescription::* id;
	std::optional<uint32_t> LevelDescription::EnvironmentRows::* levelRow;
};

const EnvironmentColumn environmentColumns[] = {
	{ "SunPosition", "sun_position", &LevelDescription::sunPositionId, &LevelDescription::EnvironmentRows::sunPosition },
	{ "SkyAtmosphere", "atmosphere", &LevelDescription::atmosphereId, &LevelDescription::EnvironmentRows::atmosphere },
	{ "PostProcessSettings", "post_process", &LevelDescription::postProcessId, &LevelDescription::EnvironmentRows::postProcess },
	{ "Wind", "wind", &LevelDescription::windId, &LevelDescription::EnvironmentRows::wind },
	{ "ExponentialHeightFog", "height_fog", &LevelDescription::heightFogId, &LevelDescription::EnvironmentRows::heightFog },
	{ "VolumetricCloud", "volumetric_cloud", &LevelDescription::volumetricCloudId, &LevelDescription::EnvironmentRows::volumetricCloud },
};

// Копия строки таблицы со всеми колонками, кроме id; name — новое имя строки (если у таблицы есть такая колонка)
uint32_t copyRow( SQLite::Database& db, const char* table, uint32_t id, const std::string& name )
{
	std::string columns;
	bool hasName = false;
	SQLite::Statement info( db, std::string( "PRAGMA table_info(" ) + table + ")" );
	while( info.executeStep() )
	{
		const std::string column = info.getColumn( 1 ).getString();
		if( column == "id" )
			continue;
		hasName = hasName || column == "name";
		columns += ( columns.empty() ? "" : ", " ) + column;
	}
	SQLite::Statement copy( db, std::string( "INSERT INTO " ) + table + " (" + columns + ") SELECT " + columns + " FROM " + table +
							" WHERE id = :id" );
	copy.bind( ":id", id );
	copy.exec();
	const uint32_t copied = static_cast<uint32_t>( db.getLastInsertRowid() );
	if( hasName )
	{
		SQLite::Statement rename( db, std::string( "UPDATE " ) + table + " SET name = :name WHERE id = :id" );
		rename.bind( ":name", name );
		rename.bind( ":id", copied );
		rename.exec();
	}
	return copied;
}

}

void LibraryLoader::loadLevelPresetNames( LevelDescription& level )
{
	level.presets.clear();
	try
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT name FROM LevelPresets WHERE level = :level ORDER BY id" );
		query.bind( ":level", level.id );
		while( query.executeStep() )
			level.presets.push_back( query.getColumn( "name" ).getString() );
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "No environment presets (table LevelPresets): " ) + e.what() );
	}
}

bool LibraryLoader::usePresetRows( LevelDescription& level, const std::string& preset )
{
	for( const EnvironmentColumn& column : environmentColumns )
		level.*column.id = level.levelRows.*column.levelRow;
	level.preset.clear();
	level.presetLocked = false;
	if( preset.empty() )
		return true;

	try
	{
		SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM LevelPresets WHERE level = :level AND name = :name" );
		query.bind( ":level", level.id );
		query.bind( ":name", preset );
		if( !query.executeStep() )
		{
			LOG( "Preset '" + preset + "' of level " + level.name + " is not found in table LevelPresets: the level rows are used" );
			return false;
		}
		for( const EnvironmentColumn& column : environmentColumns )
		{
			const SQLite::Column row = query.getColumn( column.column );
			if( !row.isNull() )
				level.*column.id = row.getUInt();
		}
		level.preset = preset;
		level.presetLocked = SettingsDatabaseDetail::hasColumn( query, "locked" ) && query.getColumn( "locked" ).getInt() != 0;
	}
	catch( const std::exception& e )
	{
		LOG( "Can`t read preset '" + preset + "': " + e.what() );
		for( const EnvironmentColumn& column : environmentColumns )
			level.*column.id = level.levelRows.*column.levelRow;
		return false;
	}
	LOG( "Environment preset: " + preset + ( level.presetLocked ? " (locked)" : "" ) );
	return true;
}

bool LibraryLoader::selectLevelPreset( LevelDescription& level, const std::string& preset )
{
	const bool found = usePresetRows( level, preset );
	// Настройки заново: чего нет в строках — значения по умолчанию, а не прежнего пресета
	level.sunPosition.reset();
	level.atmosphere = GS::SkyAtmosphereSettings();
	level.postProcess = GS::PostProcessSettings();
	level.wind.reset();
	level.heightFog.reset();
	level.volumetricCloud.reset();
	level.hdriBackdrop.reset();
	if( !loadLevelEnvironment( level ) )
		return false;
	return found;
}

bool LibraryLoader::saveLevelPreset( LevelDescription& level, const std::string& name, const LevelSaveData& data,
									 const LevelSaveData& previous )
{
	if( name.empty() || name.find( '/' ) != std::string::npos )
	{
		LOG( "Preset name '" + name + "' is empty or has '/'" );
		return false;
	}
	if( std::find( level.presets.begin(), level.presets.end(), name ) != level.presets.end() )
	{
		LOG( "Preset '" + name + "' already exists: choose another name" );
		return false;
	}

	// При ошибке уровень остаётся с прежними строками
	const LevelDescription::EnvironmentRows before = { level.sunPositionId, level.atmosphereId, level.postProcessId, level.windId,
													   level.heightFogId, level.volumetricCloudId };
	const std::string beforePreset = level.preset;
	const bool beforeLocked = level.presetLocked;
	try
	{
		SQLite::Database& db = DBConnector::instance().db();
		SQLite::Transaction transaction( db );
		// Копии строк активного пресета (в базе — их сохранённое состояние) с именем «уровень пресет»
		const std::string rowName = level.name + " " + name;
		for( const EnvironmentColumn& column : environmentColumns )
			if( level.*column.id )
				level.*column.id = copyRow( db, column.table, *( level.*column.id ), rowName );
		SQLite::Statement insert( db, "INSERT INTO LevelPresets (level, name, sun_position, atmosphere, post_process, wind, height_fog, "
									  "volumetric_cloud, locked) VALUES (:level, :name, :sun_position, :atmosphere, :post_process, "
									  ":wind, :height_fog, :volumetric_cloud, 0)" );
		insert.bind( ":level", level.id );
		insert.bind( ":name", name );
		for( const EnvironmentColumn& column : environmentColumns )
		{
			const std::string parameter = std::string( ":" ) + column.column;
			if( level.*column.id )
				insert.bind( parameter.c_str(), *( level.*column.id ) );
			else
				insert.bind( parameter.c_str() );
		}
		insert.exec();
		level.preset = name;
		level.presetLocked = false;
		// Правки окон после загрузки или сохранения — в новые строки: пресет — то, что сейчас в окнах
		saveEnvironmentRows( db, level, data, previous );
		transaction.commit();
	}
	catch( const std::exception& e )
	{
		LOG( "Can`t save preset '" + name + "': " + e.what() );
		level.sunPositionId = before.sunPosition;
		level.atmosphereId = before.atmosphere;
		level.postProcessId = before.postProcess;
		level.windId = before.wind;
		level.heightFogId = before.heightFog;
		level.volumetricCloudId = before.volumetricCloud;
		level.preset = beforePreset;
		level.presetLocked = beforeLocked;
		return false;
	}
	level.presets.push_back( name );
	LOG( "Environment preset is saved: " + name );
	return true;
}
