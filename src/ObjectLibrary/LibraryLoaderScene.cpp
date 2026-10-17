#include "LibraryLoader.h"
#include "SettingsDatabase.h"
#include <algorithm>
#include <cstdio>
#include <utility>
#include "DBConnector.h"
#include "Logger\Logger.h"
#include "Utils\utilites.h"
#include "Materials\MaterialInstanceStorage.h"
#include "Shaders\terrain_layer_constants.h"

void LibraryLoader::loadTerrainLayerRules( uint32_t terrainId, LevelDescription& level )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "TerrainLayerRules" ) )
		return;
	SQLite::Statement query( db, "SELECT * FROM TerrainLayerRules WHERE terrain = :terrain" );
	query.bind( ":terrain", terrainId );
	if( !query.executeStep() )
		return;
	// Колонки — по описанию полей правил (GS::terrainLayerRulesSchema): диапазон — <колонка>_from и _to; нет колонки или NULL —
	// значение по умолчанию
	readSettings( query, GS::terrainLayerRulesSchema(), level.terrainLayerRules, "TerrainLayerRules" );
}

void LibraryLoader::loadTerrainPaint( uint32_t terrainId, LevelDescription& level )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "TerrainPaint" ) )
		return;
	SQLite::Statement query( db, "SELECT target, tile_x, tile_z, delta FROM TerrainPaint WHERE terrain = :terrain" );
	query.bind( ":terrain", terrainId );
	while( query.executeStep() )
	{
		GS::TerrainPaintTile tile;
		tile.target = query.getColumn( 0 ).getString();
		tile.tileX = query.getColumn( 1 ).getInt();
		tile.tileZ = query.getColumn( 2 ).getInt();
		const SQLite::Column delta = query.getColumn( 3 );
		const int8_t* bytes = static_cast<const int8_t*>( delta.getBlob() );
		tile.delta.assign( bytes, bytes + delta.getBytes() );
		level.terrainPaint.push_back( std::move( tile ) );
	}
}

void LibraryLoader::loadAmbientSound( uint32_t ambientSoundId, LevelDescription& level )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "AmbientSound" ) )
		return;
	SQLite::Statement query( db, "SELECT * FROM AmbientSound WHERE id = :id" );
	query.bind( ":id", ambientSoundId );
	if( !query.executeStep() )
	{
		LOG( "Ambient sound: no row " + std::to_string( ambientSoundId ) + " in table AmbientSound" );
		return;
	}
	// Поля — по описанию (ambientSoundSchema): NULL — значения по умолчанию AmbientSoundSettings
	GS::AmbientSoundSettings& sound = level.ambientSound.emplace();
	sound.id = ambientSoundId;
	readSettings( query, GS::ambientSoundSchema(), sound, "Ambient sound " + std::to_string( ambientSoundId ) );
}

void LibraryLoader::loadSoundscape( LevelDescription& level )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "SoundscapeLayers" ) || !db.tableExists( "Sounds" ) )
		return;
	SQLite::Statement query( db, "SELECT * FROM SoundscapeLayers WHERE level = :level AND enabled = 1 ORDER BY id" );
	query.bind( ":level", level.id );
	while( query.executeStep() )
	{
		GS::SoundscapeLayerSettings& layer = level.soundscapeLayers.emplace_back();
		layer.id = query.getColumn( "id" ).getUInt();
		layer.name = query.getColumn( "name" ).getString();
		readSettings( query, GS::soundscapeLayerSchema(), layer, "Soundscape layer " + layer.name );
	}
	// Записи слоёв — по имени, каждая один раз
	SQLite::Statement sound( db, "SELECT * FROM Sounds WHERE name = :name" );
	for( const GS::SoundscapeLayerSettings& layer : level.soundscapeLayers )
	{
		if( std::any_of( level.sounds.begin(), level.sounds.end(), [&]( const GS::SoundRecord& s ) { return s.name == layer.sound; } ) )
			continue;
		sound.bind( ":name", layer.sound );
		if( sound.executeStep() )
		{
			GS::SoundRecord& record = level.sounds.emplace_back();
			record.name = layer.sound;
			record.file = sound.getColumn( "file" ).getString();
			record.phrases = sound.getColumn( "phrases" ).getString();
			record.author = sound.getColumn( "author" ).getString();
			record.license = sound.getColumn( "license" ).getString();
		}
		else
			LOG( "Soundscape layer " + layer.name + ": no sound " + layer.sound + " in table Sounds (Tools/pack_sound.py)" );
		sound.reset();
	}
}

void LibraryLoader::loadFarTerrain( uint32_t farTerrainId, LevelDescription& level )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "FarTerrain" ) )
		return;
	SQLite::Statement query( db, "SELECT * FROM FarTerrain WHERE id = :id" );
	query.bind( ":id", farTerrainId );
	if( !query.executeStep() )
	{
		LOG( "Far terrain: no row " + std::to_string( farTerrainId ) + " in table FarTerrain" );
		return;
	}
	// Поля — по описанию (farTerrainSchema): NULL — значения по умолчанию FarTerrainSettings
	GS::FarTerrainSettings& mountains = level.farTerrain.emplace();
	mountains.id = farTerrainId;
	readSettings( query, GS::farTerrainSchema(), mountains, "Far terrain " + std::to_string( farTerrainId ) );
	SQLite::Statement file( db, "select file from Textures where name = :name" );
	file.bind( ":name", mountains.heightMap );
	if( file.executeStep() )
		mountains.heightMapFile = file.getColumn( "file" ).getString();

	// Речная эрозия гор — строка FluvialErosion (FarTerrain.fluvial_erosion); NULL — без неё
	if( SettingsDatabaseDetail::hasColumn( query, "fluvial_erosion" ) && !query.getColumn( "fluvial_erosion" ).isNull() &&
		db.tableExists( "FluvialErosion" ) )
	{
		const uint32_t fluvialId = query.getColumn( "fluvial_erosion" ).getUInt();
		SQLite::Statement fluvial( db, "SELECT * FROM FluvialErosion WHERE id = :id" );
		fluvial.bind( ":id", fluvialId );
		if( fluvial.executeStep() )
		{
			GS::FluvialErosionSettings& settings = mountains.fluvial.emplace();
			settings.id = fluvialId;
			readSettings( fluvial, GS::fluvialErosionSchema(), settings, "Fluvial erosion " + std::to_string( fluvialId ) );
		}
		else
			LOG( "Far terrain: no row " + std::to_string( fluvialId ) + " in table FluvialErosion" );
	}
	// Ледниковая эрозия гор — строка GlacialErosion (FarTerrain.glacial_erosion); NULL — без неё
	if( SettingsDatabaseDetail::hasColumn( query, "glacial_erosion" ) && !query.getColumn( "glacial_erosion" ).isNull() &&
		db.tableExists( "GlacialErosion" ) )
	{
		const uint32_t glacierId = query.getColumn( "glacial_erosion" ).getUInt();
		SQLite::Statement glacier( db, "SELECT * FROM GlacialErosion WHERE id = :id" );
		glacier.bind( ":id", glacierId );
		if( glacier.executeStep() )
		{
			GS::GlacialErosionSettings& settings = mountains.glacier.emplace();
			settings.id = glacierId;
			readSettings( glacier, GS::glacialErosionSchema(), settings, "Glacial erosion " + std::to_string( glacierId ) );
		}
		else
			LOG( "Far terrain: no row " + std::to_string( glacierId ) + " in table GlacialErosion" );
	}
}

bool LibraryLoader::loadTerrain( uint32_t terrainId, LevelDescription& level )
{
	// Колонка splatmap больше не читается: splat-карту дают правила по итоговому рельефу (TerrainMasks)
	SQLite::Statement query( DBConnector::instance().db(), "SELECT * FROM Terrain WHERE id = :id" );
	query.bind( ":id", terrainId );
	if( !query.executeStep() )
	{
		LOG( "Terrain: no row " + std::to_string( terrainId ) + " in table Terrain" );
		return false;
	}
	GS::TerrainDescription terrain;
	terrain.heightMap = query.getColumn( "heightmap" ).getString();
	terrain.settings.id = terrainId;
	// Материал и сетка — по описанию полей (terrainSchema): NULL — значения по умолчанию TerrainSettings
	readSettings( query, GS::terrainSchema(), terrain.settings, "Terrain " + std::to_string( terrainId ) );
	terrain.heightOffset = static_cast<float>( query.getColumn( "height_offset" ).getDouble() );
	terrain.texelSize = static_cast<float>( query.getColumn( "width_multiplier" ).getDouble() );

	SQLite::Statement file( DBConnector::instance().db(), "select file from Textures where name = :name" );
	file.bind( ":name", terrain.heightMap );
	if( file.executeStep() )
		terrain.heightMapFile = file.getColumn( "file" ).getString();

	// Слои материала: albedo_tint — "r,g,b"; колонки нет или NULL — текстура как есть; displacement — м, NULL — без смещения;
	// occlusion — сила AO, NULL — 1; role — роль в правилах слоёв, NULL — никакой
	std::vector<GS::TerrainLayerDescription>& layers = terrain.layers;
	layers.assign( TERRAIN_MAX_LAYERS, {} );
	if( DBConnector::instance().db().tableExists( "TerrainLayers" ) )
	{
		SQLite::Statement rows( DBConnector::instance().db(), "select * from TerrainLayers where terrain = :terrain" );
		rows.bind( ":terrain", terrainId );
		while( rows.executeStep() )
		{
			const int index = rows.getColumn( "layer" ).getInt();
			if( index < 0 || index >= TERRAIN_MAX_LAYERS )
			{
				LOG( "Terrain material: layer " + std::to_string( index ) + " is out of range 0.." + std::to_string( TERRAIN_MAX_LAYERS - 1 ) );
				continue;
			}
			GS::TerrainLayerDescription& layer = layers[index];
			layer.name = rows.getColumn( "name" ).getString();
			layer.albedo = rows.getColumn( "albedo" ).getString();
			layer.normal = rows.getColumn( "normal" ).getString();
			layer.tiling = static_cast<float>( rows.getColumn( "tiling" ).getDouble() );
			for( int column = 0; column < rows.getColumnCount(); ++column )
			{
				const std::string name = rows.getColumnName( column );
				if( rows.getColumn( column ).isNull() )
					continue;
				if( name == "albedo_tint" )
					strToVec3( rows.getColumn( column ).getString(), layer.tint );
				else if( name == "displacement" )
					layer.displacement = static_cast<float>( rows.getColumn( column ).getDouble() );
				else if( name == "occlusion" )
					layer.occlusion = static_cast<float>( rows.getColumn( column ).getDouble() );
				else if( name == "role" )
					layer.role = rows.getColumn( column ).getString();
			}
		}
	}
	// Слоёв — до наибольшего описанного; пропуск в номерах — заглушка
	while( layers.size() > 1 && layers.back().albedo.empty() && layers.back().normal.empty() )
		layers.pop_back();
	for( uint32_t i = 0; i < layers.size(); ++i )
	{
		if( layers[i].albedo.empty() && layers[i].normal.empty() )
			LOG( "Terrain material: layer " + std::to_string( i ) + " is not described in TerrainLayers, placeholder is used" );
	}
	level.terrain = std::move( terrain );
	return true;
}

void LibraryLoader::loadTerrainErosion( uint32_t terrainId, LevelDescription& level )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "TerrainErosion" ) )
		return;
	SQLite::Statement query( db, "SELECT e.* FROM Terrain t JOIN TerrainErosion e ON e.id = t.erosion WHERE t.id = :terrain" );
	query.bind( ":terrain", terrainId );
	if( !query.executeStep() )
		return;
	GS::TerrainErosionSettings& erosion = level.terrainErosion.emplace();
	erosion.id = query.getColumn( "id" ).getUInt();
	readSettings( query, GS::terrainErosionSchema(), erosion, "TerrainErosion " + std::to_string( erosion.id ) );
}

void LibraryLoader::loadWaterChannels( uint32_t waterSimulationId, GS::WaterChannelsSettings& channels )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "WaterChannels" ) )
		return;
	SQLite::Statement query( db, "SELECT * FROM WaterChannels WHERE water_simulation = :id" );
	query.bind( ":id", waterSimulationId );
	if( !query.executeStep() )
		return;
	readSettings( query, GS::waterChannelsSchema(), channels, "WaterChannels" );
}

void LibraryLoader::loadStreams( uint32_t waterSimulationId, GS::WaterSimulationSettings& water )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "Streams" ) || !db.tableExists( "StreamPoints" ) )
		return;
	SQLite::Statement query( db, "SELECT * FROM Streams WHERE water_simulation = :id ORDER BY id" );
	query.bind( ":id", waterSimulationId );
	while( query.executeStep() )
	{
		GS::StreamCurve& curve = water.streams.emplace_back();
		curve.id = static_cast<uint32_t>( query.getColumn( "id" ).getInt() );
		curve.name = query.getColumn( "name" ).getString();
		curve.generated = query.getColumn( "generated" ).getInt() != 0;
		curve.edited = query.getColumn( "edited" ).getInt() != 0;
		readSettings( query, GS::streamCurveSchema(), curve, "Streams " + std::to_string( curve.id ) );
		SQLite::Statement points( db, "SELECT x, z, discharge FROM StreamPoints WHERE stream = :stream ORDER BY point" );
		points.bind( ":stream", static_cast<int64_t>( curve.id ) );
		while( points.executeStep() )
		{
			GS::StreamCurvePoint& point = curve.points.emplace_back();
			point.position = DirectX::XMFLOAT2( static_cast<float>( points.getColumn( 0 ).getDouble() ),
												static_cast<float>( points.getColumn( 1 ).getDouble() ) );
			point.discharge = points.getColumn( 2 ).isNull() ? 0.0f : static_cast<float>( points.getColumn( 2 ).getDouble() );
		}
	}
	if( db.tableExists( "StreamGeneration" ) )
	{
		SQLite::Statement key( db, "SELECT key FROM StreamGeneration WHERE water_simulation = :id" );
		key.bind( ":id", waterSimulationId );
		if( key.executeStep() )
			water.streamsKey = key.getColumn( 0 ).getString();
	}
}

bool LibraryLoader::saveGeneratedStreams( uint32_t waterSimulationId, const std::string& key, std::vector<GS::StreamCurve>& curves )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "Streams" ) || !db.tableExists( "StreamPoints" ) || !db.tableExists( "StreamGeneration" ) )
		return false;
	try
	{
		SQLite::Transaction transaction( db );
		SQLite::Statement removePoints( db, "DELETE FROM StreamPoints WHERE stream IN (SELECT id FROM Streams WHERE water_simulation = :id "
											"AND generated != 0 AND edited = 0)" );
		removePoints.bind( ":id", waterSimulationId );
		removePoints.exec();
		SQLite::Statement removeStreams( db, "DELETE FROM Streams WHERE water_simulation = :id AND generated != 0 AND edited = 0" );
		removeStreams.bind( ":id", waterSimulationId );
		removeStreams.exec();
		std::string columns = "water_simulation, name, generated, edited";
		std::string values = ":water, :name, :generated, :edited";
		for( const std::string& column : settingsColumns( GS::streamCurveSchema() ) )
		{
			columns += ", " + column;
			values += ", :" + column;
		}
		SQLite::Statement insertStream( db, "INSERT INTO Streams (" + columns + ") VALUES (" + values + ")" );
		SQLite::Statement insertPoint( db, "INSERT INTO StreamPoints (stream, point, x, z, discharge) VALUES (:stream, :point, :x, :z, :discharge)" );
		for( GS::StreamCurve& curve : curves )
		{
			if( curve.id != 0 )
				continue;
			insertStream.bind( ":water", waterSimulationId );
			insertStream.bind( ":name", curve.name );
			insertStream.bind( ":generated", curve.generated ? 1 : 0 );
			insertStream.bind( ":edited", curve.edited ? 1 : 0 );
			bindSettings( insertStream, GS::streamCurveSchema(), curve );
			insertStream.exec();
			insertStream.reset();
			curve.id = static_cast<uint32_t>( db.getLastInsertRowid() );
			for( size_t i = 0; i < curve.points.size(); ++i )
			{
				insertPoint.bind( ":stream", static_cast<int64_t>( curve.id ) );
				insertPoint.bind( ":point", static_cast<int64_t>( i ) );
				insertPoint.bind( ":x", static_cast<double>( curve.points[i].position.x ) );
				insertPoint.bind( ":z", static_cast<double>( curve.points[i].position.y ) );
				if( curve.points[i].discharge > 0.0f )
					insertPoint.bind( ":discharge", static_cast<double>( curve.points[i].discharge ) );
				else
					insertPoint.bind( ":discharge" );
				insertPoint.exec();
				insertPoint.reset();
			}
		}
		SQLite::Statement saveKey( db, "INSERT OR REPLACE INTO StreamGeneration (water_simulation, key) VALUES (:id, :key)" );
		saveKey.bind( ":id", waterSimulationId );
		saveKey.bind( ":key", key );
		saveKey.exec();
		transaction.commit();
	}
	catch( const std::exception& error )
	{
		LOG( std::string( "Can`t save stream curves: " ) + error.what() );
		return false;
	}
	return true;
}

// Объекты сцены уровня: правки из GUI («Save level») — строки LevelModels, Terrain и TerrainLayers, ScatterSets, ScatterLayers
// и ScatterLayerModels, WaterSimulation, ParticleEmitters и место LevelParticleEmitters, LevelReflectionCaptures. Только колонки, которые правятся в
// окнах, и только изменённые против снимка previous (RowUpdate); состав (какие слои, эмиттеры) не меняется, кроме
// экземпляров моделей: новые строки LevelModels вставляются, убранные удаляются

namespace
{

template<typename T>
const T* previousOf( const std::optional<T>& previous )
{
	return previous ? &*previous : nullptr;
}

// Значение по номеру слоя (TerrainLayers) или fallback, если слоя в списке нет
template<typename T>
T layerValue( const std::vector<T>& values, uint32_t layer, const T& fallback )
{
	return layer < values.size() ? values[layer] : fallback;
}

bool samePaint( const std::vector<GS::TerrainPaintTile>& a, const std::vector<GS::TerrainPaintTile>& b )
{
	if( a.size() != b.size() )
		return false;
	for( size_t i = 0; i < a.size(); ++i )
		if( a[i].target != b[i].target || a[i].tileX != b[i].tileX || a[i].tileZ != b[i].tileZ || a[i].delta != b[i].delta )
			return false;
	return true;
}

bool samePoints( const std::vector<GS::StreamCurvePoint>& a, const std::vector<GS::StreamCurvePoint>& b )
{
	if( a.size() != b.size() )
		return false;
	for( size_t i = 0; i < a.size(); ++i )
		if( a[i].position.x != b[i].position.x || a[i].position.y != b[i].position.y || a[i].discharge != b[i].discharge )
			return false;
	return true;
}

// Своя раскладка варианта слоя (ScatterLayerModels): как у слоя — NULL
DbValue optionalFloat( bool asLayer, float value )
{
	return asLayer ? DbValue( nullptr ) : dbFloat( value );
}

DbValue optionalInt( bool asLayer, int32_t value )
{
	return asLayer ? DbValue( nullptr ) : dbInt( value );
}

}

bool LibraryLoader::saveLevelScene( const LevelDescription& level, LevelSaveData& data, const LevelSaveData& previous )
{
	try
	{
		SQLite::Database& db = DBConnector::instance().db();
		SQLite::Transaction transaction( db );

		// Убранные в редакторе экземпляры — строки удаляются; добавленные (id 0) — новые строки уровня, id — обратно в
		// экземпляры: после сохранения они правятся по id, как загруженные. Прежнее у экземпляров — как в базе (снимок —
		// LevelDescription::modelInstances): пересадка на новую землю после эрозии — правка, «Save level» её закрепляет
		SQLite::Statement deleteModel( db, "DELETE FROM LevelModels WHERE id = :id" );
		for( uint32_t id : data.removedModels )
		{
			deleteModel.bind( ":id", id );
			deleteModel.exec();
			deleteModel.reset();
		}
		SQLite::Statement insertModel( db, "INSERT INTO LevelModels (level, model) VALUES (:level, :model)" );
		for( LevelDescription::ModelInstance& model : data.models )
		{
			const LevelDescription::ModelInstance* before = nullptr;
			if( model.id == 0 )
			{
				insertModel.bind( ":level", level.id );
				insertModel.bind( ":model", model.model );
				insertModel.exec();
				insertModel.reset();
				model.id = static_cast<uint32_t>( db.getLastInsertRowid() );
			}
			else
				before = findPrevious( previous.models, model.id, []( const LevelDescription::ModelInstance& m ) { return m.id; } );
			using Instance = LevelDescription::ModelInstance;
			RowUpdate update;
			update.set( "position", model, before, []( const Instance& m ) { return DbValue( vec3ToStr( m.position ) ); } );
			update.set( "rotation", model, before, []( const Instance& m )
			{
				char rotation[96];
				std::snprintf( rotation, sizeof( rotation ), "%g,%g,%g,%g", m.rotation.x, m.rotation.y, m.rotation.z, m.rotation.w );
				return DbValue( std::string( rotation ) );
			} );
			update.set( "scale", model, before, []( const Instance& m ) { return DbValue( vec3ToStr( m.scale ) ); } );
			update.update( db, "LevelModels", { { "id", dbInt( model.id ) } } );
		}

		if( data.terrain && data.terrain->id )
		{
			const GS::TerrainSettings& terrain = *data.terrain;
			const GS::TerrainSettings* before = previousOf( previous.terrain );
			// Строка Terrain, эрозия и правила слоёв — по описаниям полей
			updateSettings( db, "Terrain", terrain.id, GS::terrainSchema(), terrain, before );
			if( terrain.erosion && terrain.erosion->id )
				updateSettings( db, "TerrainErosion", terrain.erosion->id, GS::terrainErosionSchema(), *terrain.erosion,
								before ? previousOf( before->erosion ) : nullptr );

			// Правила слоёв: строка TerrainLayerRules террейна (нет — появится)
			if( db.tableExists( "TerrainLayerRules" ) )
				RowUpdate().settings( GS::terrainLayerRulesSchema(), terrain.layerRules, before ? &before->layerRules : nullptr )
					.upsert( db, "TerrainLayerRules", { { "terrain", dbInt( terrain.id ) } } );

			// Мазки кисти: правили — плитки террейна заново (убранные мазки — убранные строки)
			if( db.tableExists( "TerrainPaint" ) && !( before && samePaint( terrain.paint, before->paint ) ) )
			{
				SQLite::Statement removePaint( db, "DELETE FROM TerrainPaint WHERE terrain = :terrain" );
				removePaint.bind( ":terrain", terrain.id );
				removePaint.exec();
				SQLite::Statement insertPaint( db, "INSERT INTO TerrainPaint (terrain, target, tile_x, tile_z, delta) "
												   "VALUES (:terrain, :target, :x, :z, :delta)" );
				for( const GS::TerrainPaintTile& tile : terrain.paint )
				{
					insertPaint.bind( ":terrain", terrain.id );
					insertPaint.bind( ":target", tile.target );
					insertPaint.bind( ":x", tile.tileX );
					insertPaint.bind( ":z", tile.tileZ );
					insertPaint.bind( ":delta", tile.delta.data(), static_cast<int>( tile.delta.size() ) );
					insertPaint.exec();
					insertPaint.reset();
				}
			}

			// Слои материала: повтор, оттенок, дисплейсмент, затенение — по номеру слоя
			for( uint32_t layer = 0; layer < terrain.layerTiling.size(); ++layer )
			{
				if( terrain.layerTiling[layer] <= 0.0f )
					continue;
				const auto tiling = [layer]( const GS::TerrainSettings& t ) { return dbFloat( layerValue( t.layerTiling, layer, 0.0f ) ); };
				const auto tint = [layer]( const GS::TerrainSettings& t )
				{
					return DbValue( vec3ToStr( layerValue( t.layerTint, layer, DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f ) ) ) );
				};
				const auto displacement = [layer]( const GS::TerrainSettings& t ) { return dbFloat( layerValue( t.layerDisplacement, layer, 0.0f ) ); };
				const auto occlusion = [layer]( const GS::TerrainSettings& t ) { return dbFloat( layerValue( t.layerOcclusion, layer, 1.0f ) ); };
				RowUpdate update;
				update.set( "tiling", terrain, before, tiling );
				update.set( "albedo_tint", terrain, before, tint );
				update.set( "displacement", terrain, before, displacement );
				update.set( "occlusion", terrain, before, occlusion );
				update.update( db, "TerrainLayers", { { "terrain", dbInt( terrain.id ) }, { "layer", dbInt( layer ) } } );
			}
		}

		for( const GS::ScatterSetSettings& set : data.scatterSets )
			if( set.id != 0 )
				updateSettings( db, "ScatterSets", set.id, GS::scatterSetSchema(), set,
								findPrevious( previous.scatterSets, set.id, []( const GS::ScatterSetSettings& s ) { return s.id; } ) );

		// Слой — по описанию полей (scatterLayerSchema), его варианты — колонки ScatterLayerModels
		for( const GS::ScatterLayerRecord& record : data.scatterLayers )
		{
			const GS::ScatterLayerSettings& layer = record.settings;
			if( layer.id == 0 )
				continue;
			const GS::ScatterLayerRecord* before = findPrevious( previous.scatterLayers, layer.id,
																 []( const GS::ScatterLayerRecord& r ) { return r.settings.id; } );
			updateSettings( db, "ScatterLayers", layer.id, GS::scatterLayerSchema(), layer, before ? &before->settings : nullptr );

			using Model = GS::ScatterModelSettings;
			for( const Model& model : record.models )
			{
				if( model.id == 0 )
					continue;
				const Model* modelBefore = before ? findPrevious( before->models, model.id, []( const Model& m ) { return m.id; } ) : nullptr;
				RowUpdate update;
				update.set( "weight", model, modelBefore, []( const Model& m ) { return dbFloat( m.weight ); } );
				update.set( "cast_shadow", model, modelBefore, []( const Model& m ) { return dbInt( m.castShadow ? 1 : 0 ); } );
				// Свои границы LOD — текстом «6,14»; совпадают с моделью — NULL
				update.set( "lod_distances", model, modelBefore, []( const Model& m )
				{
					std::string lods;
					for( float distance : m.lodDistances )
					{
						char text[32];
						snprintf( text, sizeof( text ), "%g", distance );
						lods += ( lods.empty() ? "" : "," ) + std::string( text );
					}
					return dbText( lods );
				} );
				// Своя раскладка: как у слоя — NULL
				update.set( "size_multiplier", model, modelBefore,
							[]( const Model& m ) { return optionalFloat( m.placement.sizeMultiplier == 1.0f, m.placement.sizeMultiplier ); } );
				update.set( "size_variation", model, modelBefore,
							[]( const Model& m ) { return optionalFloat( m.placement.sizeVariation < 0.0f, m.placement.sizeVariation ); } );
				update.set( "align_to_terrain", model, modelBefore,
							[]( const Model& m ) { return optionalInt( m.placement.alignToTerrain < 0, m.placement.alignToTerrain ); } );
				update.set( "patch_group", model, modelBefore,
							[]( const Model& m ) { return optionalInt( m.placement.patchGroup < 1, m.placement.patchGroup ); } );
				update.set( "ground_layers", model, modelBefore, []( const Model& m ) { return dbText( m.placement.groundLayers ); } );
				update.set( "ground_preference", model, modelBefore, []( const Model& m )
				{
					return optionalFloat( m.placement.groundLayers.empty(), m.placement.groundPreference );
				} );
				update.update( db, "ScatterLayerModels", { { "id", dbInt( model.id ) } } );
			}
		}

		if( data.farTerrain && level.farTerrain )
		{
			updateSettings( db, "FarTerrain", level.farTerrain->id, GS::farTerrainSchema(), *data.farTerrain, previousOf( previous.farTerrain ) );
			if( data.farTerrain->fluvial && level.farTerrain->fluvial )
				updateSettings( db, "FluvialErosion", level.farTerrain->fluvial->id, GS::fluvialErosionSchema(), *data.farTerrain->fluvial,
								previous.farTerrain && previous.farTerrain->fluvial ? &*previous.farTerrain->fluvial : nullptr );
			if( data.farTerrain->glacier && level.farTerrain->glacier )
				updateSettings( db, "GlacialErosion", level.farTerrain->glacier->id, GS::glacialErosionSchema(), *data.farTerrain->glacier,
								previous.farTerrain && previous.farTerrain->glacier ? &*previous.farTerrain->glacier : nullptr );
		}

		// Слои певцов — их строки SoundscapeLayers
		using Layer = GS::SoundscapeLayerSettings;
		for( const Layer& layer : data.soundscapeLayers )
			if( layer.id != 0 )
				updateSettings( db, "SoundscapeLayers", layer.id, GS::soundscapeLayerSchema(), layer,
								findPrevious( previous.soundscapeLayers, layer.id, []( const Layer& l ) { return l.id; } ) );

		if( data.ambientSound && level.ambientSound )
			updateSettings( db, "AmbientSound", level.ambientSound->id, GS::ambientSoundSchema(), *data.ambientSound,
							previousOf( previous.ambientSound ) );

		if( data.water && level.waterSimulationId )
		{
			const GS::WaterSimulationSettings& water = *data.water;
			const GS::WaterSimulationSettings* before = previousOf( previous.water );
			const int64_t waterId = *level.waterSimulationId;
			updateSettings( db, "WaterSimulation", waterId, GS::waterSimulationSchema(), water, before );
			updateSettings( db, "WaterSimulation", waterId, GS::waterFlowSchema(), water.flow, before ? &before->flow : nullptr );

			// Русла конвейера: строка на строку WaterSimulation (нет — появится)
			RowUpdate().settings( GS::waterChannelsSchema(), water.channels, before ? &before->channels : nullptr )
				.upsert( db, "WaterChannels", { { "water_simulation", dbInt( waterId ) } } );

			// Кривые русел: включение, правка и множители, точки — если их правили во вьюпорте (Tools/stream_edit.py правит их
			// в базе)
			if( db.tableExists( "Streams" ) )
			{
				for( const GS::StreamCurve& curve : water.streams )
				{
					if( curve.id == 0 )
						continue;
					const GS::StreamCurve* curveBefore = before ? findPrevious( before->streams, curve.id,
																			   []( const GS::StreamCurve& c ) { return c.id; } ) : nullptr;
					RowUpdate update;
					update.set( "edited", curve, curveBefore, []( const GS::StreamCurve& c ) { return dbInt( c.edited ? 1 : 0 ); } );
					update.settings( GS::streamCurveSchema(), curve, curveBefore );
					update.update( db, "Streams", { { "id", dbInt( curve.id ) } } );
					const bool pointsEdited = curveBefore ? !samePoints( curve.points, curveBefore->points ) : curve.pointsChanged;
					if( !pointsEdited )
						continue;
					SQLite::Statement removePoints( db, "DELETE FROM StreamPoints WHERE stream = :stream" );
					removePoints.bind( ":stream", static_cast<int64_t>( curve.id ) );
					removePoints.exec();
					SQLite::Statement insertPoint( db, "INSERT INTO StreamPoints (stream, point, x, z, discharge) VALUES (:stream, :point, :x, :z, :discharge)" );
					for( size_t i = 0; i < curve.points.size(); ++i )
					{
						insertPoint.bind( ":stream", static_cast<int64_t>( curve.id ) );
						insertPoint.bind( ":point", static_cast<int64_t>( i ) );
						insertPoint.bind( ":x", static_cast<double>( curve.points[i].position.x ) );
						insertPoint.bind( ":z", static_cast<double>( curve.points[i].position.y ) );
						if( curve.points[i].discharge > 0.0f )
							insertPoint.bind( ":discharge", static_cast<double>( curve.points[i].discharge ) );
						else
							insertPoint.bind( ":discharge" );
						insertPoint.exec();
						insertPoint.reset();
					}
				}
			}
		}

		// Эмиттер — тип (ParticleEmitters), экземпляры уровня ссылаются на него: у двух экземпляров одного типа
		// сохранится последний
		using Emitter = GS::ParticleEmitterSettings;
		for( const Emitter& emitter : data.emitters )
			if( emitter.id != 0 )
				updateSettings( db, "ParticleEmitters", emitter.id, GS::particleEmitterSchema(), emitter,
								findPrevious( previous.emitters, emitter.instanceId, []( const Emitter& e ) { return e.instanceId; } ) );
		// Место экземпляра — у эмиттера в точке и в шаре (гизмо вьюпорта, свойство Position); у поля вокруг камеры его нет
		for( const Emitter& emitter : data.emitters )
		{
			if( emitter.instanceId == 0 || ( emitter.spawn != Emitter::Spawn::point && emitter.spawn != Emitter::Spawn::sphere ) )
				continue;
			const Emitter* before = findPrevious( previous.emitters, emitter.instanceId, []( const Emitter& e ) { return e.instanceId; } );
			RowUpdate()
				.set( "position", emitter, before, []( const Emitter& e ) { return DbValue( vec3ToStr( e.position ) ); } )
				.update( db, "LevelParticleEmitters", { { "id", dbInt( emitter.instanceId ) } } );
		}

		// Карты отражений места — их строки LevelReflectionCaptures
		using Capture = GS::ReflectionCaptureSettings;
		for( const Capture& capture : data.reflectionCaptures )
			if( capture.id != 0 )
				updateSettings( db, "LevelReflectionCaptures", capture.id, GS::reflectionCaptureSchema(), capture,
								findPrevious( previous.reflectionCaptures, capture.id, []( const Capture& c ) { return c.id; } ) );

		transaction.commit();
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Save level scene: " ) + e.what() );
		return false;
	}
	return true;
}

namespace
{

// Значение параметра материала строкой базы — как его читает setParamFromString (LibraryLoader.cpp): "true", "0.5",
// "1,1,1", id текстуры
std::string materialParamText( Property& property )
{
	char text[64];
	switch( property.valueType() )
	{
		case ValueType::BOOL:
			return property.data<bool>() ? "true" : "false";
		case ValueType::FLOAT:
			snprintf( text, sizeof( text ), "%g", property.data<float>() );
			return text;
		case ValueType::VECTOR2:
		{
			const DirectX::XMFLOAT2 value = property.data<DirectX::XMFLOAT2>();
			snprintf( text, sizeof( text ), "%g,%g", value.x, value.y );
			return text;
		}
		case ValueType::VECTOR3:
			return vec3ToStr( property.data<DirectX::XMFLOAT3>() );
		case ValueType::VECTOR4:
			return vec4ToStr( property.data<DirectX::XMFLOAT4>() );
		case ValueType::INT:
			return std::to_string( property.data<int32_t>() );
		case ValueType::UINT:
			return std::to_string( property.data<uint32_t>() );
	}
	return std::string();
}

}

bool LibraryLoader::saveMaterialInstances( const GS::MaterialInstanceStorage& instances )
{
	try
	{
		SQLite::Database& db = DBConnector::instance().db();
		SQLite::Transaction transaction( db );
		SQLite::Statement findDefinition( db, "SELECT id_parameter_def FROM MaterialParameterDef WHERE id_material = :material AND param_name = :name" );
		SQLite::Statement deleteValue( db, "DELETE FROM MaterialParameterInstance WHERE id_instance = :instance AND id_material_def = :definition" );
		SQLite::Statement insertValue( db, "INSERT INTO MaterialParameterInstance (id_instance, id_material_def, value) "
										   "VALUES (:instance, :definition, :value)" );
		uint32_t written = 0;
		for( const auto& instance : instances.all() )
		{
			for( const std::string& name : instance->params.names() )
			{
				Property& property = instance->params[name];
				if( !property.modified() )
					continue;
				findDefinition.bind( ":material", instance->material );
				findDefinition.bind( ":name", name );
				const bool found = findDefinition.executeStep();
				const int definition = found ? findDefinition.getColumn( 0 ).getInt() : 0;
				findDefinition.reset();
				if( !found )
				{
					LOG( "Material instance " + std::to_string( instance->id ) + ": parameter " + name + " has no definition, not saved" );
					continue;
				}
				deleteValue.bind( ":instance", instance->id );
				deleteValue.bind( ":definition", definition );
				deleteValue.exec();
				deleteValue.reset();
				insertValue.bind( ":instance", instance->id );
				insertValue.bind( ":definition", definition );
				insertValue.bind( ":value", materialParamText( property ) );
				insertValue.exec();
				insertValue.reset();
				++written;
			}
		}
		transaction.commit();
		if( written > 0 )
			LOG( "Material instances: " + std::to_string( written ) + " parameters saved" );
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Save material instances: " ) + e.what() );
		return false;
	}
	return true;
}
