#include "LibraryLoader.h"
#include "SettingsDatabase.h"
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

// Объекты сцены уровня: правки из GUI («Save level») — строки LevelModels, Terrain и TerrainLayers, ScatterSets, ScatterLayers и ScatterLayerModels,
// WaterSimulation, ParticleEmitters и место LevelParticleEmitters. Только колонки, которые правятся в окнах; состав (какие слои,
// эмиттеры) не меняется, кроме экземпляров моделей: новые строки LevelModels вставляются, убранные удаляются

bool LibraryLoader::saveLevelScene( const LevelDescription& level, std::vector<LevelDescription::ModelInstance>& models,
									const std::vector<uint32_t>& removedModels, const std::optional<GS::TerrainSettings>& terrain,
									const std::vector<GS::ScatterSetSettings>& scatterSets,
									const std::vector<GS::ScatterLayerRecord>& scatterLayers,
									const std::optional<GS::WaterSimulationSettings>& water,
									const std::vector<GS::ParticleEmitterSettings>& particleEmitters )
{
	try
	{
		SQLite::Database& db = DBConnector::instance().db();
		SQLite::Transaction transaction( db );

		// Убранные в редакторе экземпляры — строки удаляются; добавленные (id 0) — новые строки уровня, id — обратно в
		// экземпляры: после сохранения они правятся по id, как загруженные
		SQLite::Statement deleteModel( db, "DELETE FROM LevelModels WHERE id = :id" );
		for( uint32_t id : removedModels )
		{
			deleteModel.bind( ":id", id );
			deleteModel.exec();
			deleteModel.reset();
		}
		SQLite::Statement insertModel( db, "INSERT INTO LevelModels (level, model) VALUES (:level, :model)" );
		SQLite::Statement updateModel( db, "UPDATE LevelModels SET position = :position, rotation = :rotation, scale = :scale WHERE id = :id" );
		for( LevelDescription::ModelInstance& model : models )
		{
			if( model.id == 0 )
			{
				insertModel.bind( ":level", level.id );
				insertModel.bind( ":model", model.model );
				insertModel.exec();
				insertModel.reset();
				model.id = static_cast<uint32_t>( db.getLastInsertRowid() );
			}
			char rotation[96];
			std::snprintf( rotation, sizeof( rotation ), "%g,%g,%g,%g", model.rotation.x, model.rotation.y, model.rotation.z, model.rotation.w );
			updateModel.bind( ":position", vec3ToStr( model.position ) );
			updateModel.bind( ":rotation", std::string( rotation ) );
			updateModel.bind( ":scale", vec3ToStr( model.scale ) );
			updateModel.bind( ":id", model.id );
			updateModel.exec();
			updateModel.reset();
		}

		if( terrain && terrain->id )
		{
			// Строка Terrain, эрозия и правила слоёв — по описаниям полей
			updateSettings( db, "Terrain", terrain->id, GS::terrainSchema(), *terrain );
			if( terrain->erosion && terrain->erosion->id )
				updateSettings( db, "TerrainErosion", terrain->erosion->id, GS::terrainErosionSchema(), *terrain->erosion );

			// Правила слоёв: строка TerrainLayerRules террейна (нет — появится)
			if( db.tableExists( "TerrainLayerRules" ) )
			{
				std::string columns = "terrain";
				std::string values = ":terrain";
				for( const std::string& column : settingsColumns( GS::terrainLayerRulesSchema() ) )
				{
					columns += ", " + column;
					values += ", :" + column;
				}
				SQLite::Statement updateRules( db, "INSERT OR REPLACE INTO TerrainLayerRules (" + columns + ") VALUES (" + values + ")" );
				updateRules.bind( ":terrain", terrain->id );
				bindSettings( updateRules, GS::terrainLayerRulesSchema(), terrain->layerRules );
				updateRules.exec();
			}

			// Мазки кисти: плитки террейна заново (убранные мазки — убранные строки)
			if( db.tableExists( "TerrainPaint" ) )
			{
				SQLite::Statement removePaint( db, "DELETE FROM TerrainPaint WHERE terrain = :terrain" );
				removePaint.bind( ":terrain", terrain->id );
				removePaint.exec();
				SQLite::Statement insertPaint( db, "INSERT INTO TerrainPaint (terrain, target, tile_x, tile_z, delta) "
												   "VALUES (:terrain, :target, :x, :z, :delta)" );
				for( const GS::TerrainPaintTile& tile : terrain->paint )
				{
					insertPaint.bind( ":terrain", terrain->id );
					insertPaint.bind( ":target", tile.target );
					insertPaint.bind( ":x", tile.tileX );
					insertPaint.bind( ":z", tile.tileZ );
					insertPaint.bind( ":delta", tile.delta.data(), static_cast<int>( tile.delta.size() ) );
					insertPaint.exec();
					insertPaint.reset();
				}
			}

			SQLite::Statement updateLayer( db, "UPDATE TerrainLayers SET tiling = :tiling, albedo_tint = :tint, displacement = :displacement, "
											   "occlusion = :occlusion WHERE terrain = :terrain AND layer = :layer" );
			for( uint32_t layer = 0; layer < terrain->layerTiling.size(); ++layer )
			{
				if( terrain->layerTiling[layer] <= 0.0f )
					continue;
				updateLayer.bind( ":tiling", dbValue( terrain->layerTiling[layer] ) );
				updateLayer.bind( ":tint", layer < terrain->layerTint.size() ? vec3ToStr( terrain->layerTint[layer] ) : std::string( "1,1,1" ) );
				updateLayer.bind( ":displacement", dbValue( layer < terrain->layerDisplacement.size() ? terrain->layerDisplacement[layer] : 0.0f ) );
				updateLayer.bind( ":occlusion", dbValue( layer < terrain->layerOcclusion.size() ? terrain->layerOcclusion[layer] : 1.0f ) );
				updateLayer.bind( ":terrain", terrain->id );
				updateLayer.bind( ":layer", layer );
				updateLayer.exec();
				updateLayer.reset();
			}
		}

		for( const GS::ScatterSetSettings& set : scatterSets )
			if( set.id != 0 )
				updateSettings( db, "ScatterSets", set.id, GS::scatterSetSchema(), set );

		// Слой — по описанию полей (scatterLayerSchema)
		SQLite::Statement updateScatter( db, "UPDATE ScatterLayers SET " + settingsAssignments( GS::scatterLayerSchema() ) + " WHERE id = :id" );
		SQLite::Statement updateScatterModel( db, "UPDATE ScatterLayerModels SET weight = :weight, cast_shadow = :castShadow, "
												  "lod_distances = :lods, size_multiplier = :size, size_variation = :sizeVariation, "
												  "align_to_terrain = :align, patch_group = :patchGroup, ground_layers = :groundLayers, "
												  "ground_preference = :groundPreference WHERE id = :id" );
		for( const GS::ScatterLayerRecord& record : scatterLayers )
		{
			const GS::ScatterLayerSettings& layer = record.settings;
			if( layer.id == 0 )
				continue;
			bindSettings( updateScatter, GS::scatterLayerSchema(), layer );
			updateScatter.bind( ":id", layer.id );
			updateScatter.exec();
			updateScatter.reset();

			for( const GS::ScatterModelSettings& model : record.models )
			{
				if( model.id == 0 )
					continue;
				updateScatterModel.bind( ":weight", dbValue( model.weight ) );
				updateScatterModel.bind( ":castShadow", model.castShadow ? 1 : 0 );
				// Свои границы LOD — текстом «6,14»; совпадают с моделью — NULL
				if( model.lodDistances.empty() )
					updateScatterModel.bind( ":lods" );
				else
				{
					std::string lods;
					for( float distance : model.lodDistances )
					{
						char text[32];
						snprintf( text, sizeof( text ), "%g", distance );
						lods += ( lods.empty() ? "" : "," ) + std::string( text );
					}
					updateScatterModel.bind( ":lods", lods );
				}
				// Своя раскладка: как у слоя — NULL
				const GS::ScatterVariantPlacement& placement = model.placement;
				if( placement.sizeMultiplier == 1.0f )
					updateScatterModel.bind( ":size" );
				else
					updateScatterModel.bind( ":size", dbValue( placement.sizeMultiplier ) );
				if( placement.sizeVariation < 0.0f )
					updateScatterModel.bind( ":sizeVariation" );
				else
					updateScatterModel.bind( ":sizeVariation", dbValue( placement.sizeVariation ) );
				if( placement.alignToTerrain < 0 )
					updateScatterModel.bind( ":align" );
				else
					updateScatterModel.bind( ":align", placement.alignToTerrain );
				if( placement.patchGroup < 1 )
					updateScatterModel.bind( ":patchGroup" );
				else
					updateScatterModel.bind( ":patchGroup", placement.patchGroup );
				if( placement.groundLayers.empty() )
				{
					updateScatterModel.bind( ":groundLayers" );
					updateScatterModel.bind( ":groundPreference" );
				}
				else
				{
					updateScatterModel.bind( ":groundLayers", placement.groundLayers );
					updateScatterModel.bind( ":groundPreference", dbValue( placement.groundPreference ) );
				}
				updateScatterModel.bind( ":id", model.id );
				updateScatterModel.exec();
				updateScatterModel.reset();
			}
		}

		if( water && level.waterSimulationId )
		{
			updateSettings( db, "WaterSimulation", *level.waterSimulationId, GS::waterSimulationSchema(), *water );
			updateSettings( db, "WaterSimulation", *level.waterSimulationId, GS::waterFlowSchema(), water->flow );

			// Русла конвейера: строка на строку WaterSimulation (нет — создаётся)
			std::string columns = "water_simulation";
			std::string values = ":id";
			for( const std::string& column : settingsColumns( GS::waterChannelsSchema() ) )
			{
				columns += ", " + column;
				values += ", :" + column;
			}
			SQLite::Statement saveChannels( db, "INSERT OR REPLACE INTO WaterChannels (" + columns + ") VALUES (" + values + ")" );
			saveChannels.bind( ":id", *level.waterSimulationId );
			bindSettings( saveChannels, GS::waterChannelsSchema(), water->channels );
			saveChannels.exec();

			// Кривые русел: включение, правка и множители (точки правит Tools/stream_edit.py)
			if( db.tableExists( "Streams" ) )
			{
				SQLite::Statement updateStream( db, "UPDATE Streams SET edited = :edited, " + settingsAssignments( GS::streamCurveSchema() ) +
													" WHERE id = :id" );
				for( const GS::StreamCurve& curve : water->streams )
				{
					if( curve.id == 0 )
						continue;
					updateStream.bind( ":edited", curve.edited ? 1 : 0 );
					bindSettings( updateStream, GS::streamCurveSchema(), curve );
					updateStream.bind( ":id", static_cast<int64_t>( curve.id ) );
					updateStream.exec();
					updateStream.reset();
					// Точки правили во вьюпорте — переписать
					if( curve.pointsChanged )
					{
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
		}

		// Эмиттер — тип (ParticleEmitters), экземпляры уровня ссылаются на него: у двух экземпляров одного типа
		// сохранится последний
		for( const GS::ParticleEmitterSettings& emitter : particleEmitters )
			if( emitter.id != 0 )
				updateSettings( db, "ParticleEmitters", emitter.id, GS::particleEmitterSchema(), emitter );
		// Место экземпляра — у эмиттера в точке и в шаре (гизмо вьюпорта, свойство Position); у поля вокруг камеры его нет
		SQLite::Statement updateInstance( db, "UPDATE LevelParticleEmitters SET position = :position WHERE id = :id" );
		for( const GS::ParticleEmitterSettings& emitter : particleEmitters )
		{
			using Spawn = GS::ParticleEmitterSettings::Spawn;
			if( emitter.instanceId == 0 || ( emitter.spawn != Spawn::point && emitter.spawn != Spawn::sphere ) )
				continue;
			updateInstance.bind( ":position", vec3ToStr( emitter.position ) );
			updateInstance.bind( ":id", emitter.instanceId );
			updateInstance.exec();
			updateInstance.reset();
		}

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
