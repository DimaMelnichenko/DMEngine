#include "LibraryLoader.h"
#include <cstdio>
#include <utility>
#include "DBConnector.h"
#include "Logger\Logger.h"
#include "Utils\utilites.h"

namespace
{

// Колонки WaterChannels ↔ поля WaterChannelsSettings — одна таблица для загрузки и сохранения
const std::pair<const char*, float GS::WaterChannelsSettings::*> channelColumns[] = {
	{ "min_discharge", &GS::WaterChannelsSettings::minDischarge },
	{ "width_coef", &GS::WaterChannelsSettings::widthCoef },
	{ "min_width", &GS::WaterChannelsSettings::minWidth },
	{ "depth_coef", &GS::WaterChannelsSettings::depthCoef },
	{ "min_incision", &GS::WaterChannelsSettings::minIncision },
	{ "bank_slope", &GS::WaterChannelsSettings::bankSlope },
	{ "meander_length", &GS::WaterChannelsSettings::meanderLength },
	{ "meander_amplitude", &GS::WaterChannelsSettings::meanderAmplitude },
	{ "meander_max_slope", &GS::WaterChannelsSettings::meanderMaxSlope },
	{ "min_slope", &GS::WaterChannelsSettings::minSlope },
	{ "lake_depth", &GS::WaterChannelsSettings::lakeDepth },
	{ "min_lake_area", &GS::WaterChannelsSettings::minLakeArea },
	{ "thalweg", &GS::WaterChannelsSettings::thalweg },
	{ "manning", &GS::WaterChannelsSettings::manning },
	{ "min_water_slope", &GS::WaterChannelsSettings::minWaterSlope },
	{ "min_speed", &GS::WaterChannelsSettings::minSpeed },
	{ "min_water_depth", &GS::WaterChannelsSettings::minWaterDepth },
	{ "foam_slope", &GS::WaterChannelsSettings::foamSlope },
	{ "freeboard", &GS::WaterChannelsSettings::freeboard },
	{ "source_taper", &GS::WaterChannelsSettings::sourceTaper },
	{ "bed_shape", &GS::WaterChannelsSettings::bedShape },
	{ "bank_roundness", &GS::WaterChannelsSettings::bankRoundness },
	{ "bend_asymmetry", &GS::WaterChannelsSettings::bendAsymmetry },
	{ "pool_depth", &GS::WaterChannelsSettings::poolDepth },
	{ "pool_spacing", &GS::WaterChannelsSettings::poolSpacing },
	{ "bed_noise", &GS::WaterChannelsSettings::bedNoise },
	{ "bed_noise_scale", &GS::WaterChannelsSettings::bedNoiseScale },
	{ "step_height", &GS::WaterChannelsSettings::stepHeight },
	{ "step_steepness", &GS::WaterChannelsSettings::stepSteepness },
	{ "step_min_slope", &GS::WaterChannelsSettings::stepMinSlope },
	{ "lake_depth_ratio", &GS::WaterChannelsSettings::lakeDepthRatio },
	{ "lake_min_depth", &GS::WaterChannelsSettings::lakeMinDepth },
	{ "lake_max_depth", &GS::WaterChannelsSettings::lakeMaxDepth },
	{ "lake_shelf_width", &GS::WaterChannelsSettings::lakeShelfWidth },
	{ "lake_shelf_depth", &GS::WaterChannelsSettings::lakeShelfDepth },
	{ "lake_drop_slope", &GS::WaterChannelsSettings::lakeDropSlope },
	{ "lake_freeboard", &GS::WaterChannelsSettings::lakeFreeboard },
	{ "stone_density", &GS::WaterChannelsSettings::stoneDensity },
	{ "stone_size", &GS::WaterChannelsSettings::stoneSize },
	{ "bank_stones", &GS::WaterChannelsSettings::bankStones },
	{ "boulder_density", &GS::WaterChannelsSettings::boulderDensity },
	{ "boulder_size", &GS::WaterChannelsSettings::boulderSize },
};

// Колонки TerrainErosion ↔ поля TerrainErosionSettings
const std::pair<const char*, float GS::TerrainErosionSettings::*> erosionFloatColumns[] = {
	{ "inertia", &GS::TerrainErosionSettings::inertia },
	{ "capacity", &GS::TerrainErosionSettings::capacity },
	{ "min_slope", &GS::TerrainErosionSettings::minSlope },
	{ "erode_speed", &GS::TerrainErosionSettings::erodeSpeed },
	{ "deposit_speed", &GS::TerrainErosionSettings::depositSpeed },
	{ "evaporation", &GS::TerrainErosionSettings::evaporation },
	{ "gravity", &GS::TerrainErosionSettings::gravity },
	{ "rain_scale", &GS::TerrainErosionSettings::rainScale },
	{ "rain_min", &GS::TerrainErosionSettings::rainMin },
	{ "talus_angle", &GS::TerrainErosionSettings::talusAngle },
	{ "thermal_rate", &GS::TerrainErosionSettings::thermalRate },
};
const std::pair<const char*, int32_t GS::TerrainErosionSettings::*> erosionIntColumns[] = {
	{ "droplets", &GS::TerrainErosionSettings::droplets },
	{ "lifetime", &GS::TerrainErosionSettings::lifetime },
	{ "radius", &GS::TerrainErosionSettings::radius },
	{ "thermal_iterations", &GS::TerrainErosionSettings::thermalIterations },
};

}

void LibraryLoader::loadTerrainLayerRules( uint32_t terrainId, LevelDescription& level )
{
	SQLite::Database& db = DBConnector::instance().db();
	if( !db.tableExists( "TerrainLayerRules" ) )
		return;
	SQLite::Statement query( db, "SELECT * FROM TerrainLayerRules WHERE terrain = :terrain" );
	query.bind( ":terrain", terrainId );
	if( !query.executeStep() )
		return;
	// Колонки — по таблице полей правил (GS::terrainLayerRuleFields): диапазон — <колонка>_from и _to; нет колонки или NULL —
	// значение по умолчанию
	GS::TerrainLayerRules& rules = level.terrainLayerRules;
	auto read = [&query]( const std::string& column, float& value )
	{
		const int index = query.getColumnIndex( column.c_str() );
		if( !query.getColumn( index ).isNull() )
			value = static_cast<float>( query.getColumn( index ).getDouble() );
	};
	for( const GS::TerrainLayerRuleField& field : GS::terrainLayerRuleFields() )
	{
		const std::string column = field.column;
		try
		{
			if( field.range )
			{
				read( column + "_from", ( rules.*field.range ).from );
				read( column + "_to", ( rules.*field.range ).to );
			}
			else
				read( column, rules.*field.value );
		}
		catch( const SQLite::Exception& )
		{
			LOG( "TerrainLayerRules: no column " + column + ", the default is used" );
		}
	}
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
	for( const auto& [column, field] : erosionFloatColumns )
		if( !query.getColumn( column ).isNull() )
			erosion.*field = static_cast<float>( query.getColumn( column ).getDouble() );
	for( const auto& [column, field] : erosionIntColumns )
		if( !query.getColumn( column ).isNull() )
			erosion.*field = query.getColumn( column ).getInt();
	if( !query.getColumn( "seed" ).isNull() )
		erosion.seed = static_cast<uint32_t>( query.getColumn( "seed" ).getInt64() );
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
	for( const auto& [column, field] : channelColumns )
		if( !query.getColumn( column ).isNull() )
			channels.*field = static_cast<float>( query.getColumn( column ).getDouble() );
	if( !query.getColumn( "smooth" ).isNull() )
		channels.smooth = query.getColumn( "smooth" ).getInt();
}

// Множители кривой русла ↔ колонки Streams — одна таблица для загрузки и сохранения
const std::pair<const char*, float GS::StreamCurve::*> streamScaleColumns[] = {
	{ "width_scale", &GS::StreamCurve::widthScale },
	{ "depth_scale", &GS::StreamCurve::depthScale },
	{ "freeboard_scale", &GS::StreamCurve::freeboardScale },
	{ "bank_slope_scale", &GS::StreamCurve::bankSlopeScale },
	{ "thalweg_scale", &GS::StreamCurve::thalwegScale },
	{ "roughness_scale", &GS::StreamCurve::roughnessScale },
	{ "discharge_scale", &GS::StreamCurve::dischargeScale },
	{ "stone_scale", &GS::StreamCurve::stoneScale },
};

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
		curve.enabled = query.getColumn( "enabled" ).getInt() != 0;
		for( const auto& [column, field] : streamScaleColumns )
			if( !query.getColumn( column ).isNull() )
				curve.*field = static_cast<float>( query.getColumn( column ).getDouble() );
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
		std::string columns = "water_simulation, name, generated, edited, enabled";
		std::string values = ":water, :name, :generated, :edited, :enabled";
		for( const auto& [column, field] : streamScaleColumns )
		{
			columns += std::string( ", " ) + column;
			values += std::string( ", :" ) + column;
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
			insertStream.bind( ":enabled", curve.enabled ? 1 : 0 );
			for( const auto& [column, field] : streamScaleColumns )
				insertStream.bind( std::string( ":" ) + column, static_cast<double>( curve.*field ) );
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
			SQLite::Statement updateTerrain( db, "UPDATE Terrain SET height_multiplier = :heightMultiplier, "
												 "triplanar_sharpness = :triplanar, height_blend = :heightBlend, "
												 "far_texture_scale = :farScale, far_blend_start = :farStart, far_blend_end = :farEnd, "
												 "displacement_start = :displacementStart, displacement_end = :displacementEnd, "
												 "micro_shadows = :microShadows WHERE id = :id" );
			updateTerrain.bind( ":heightMultiplier", dbValue( terrain->heightMultiplier ) );
			updateTerrain.bind( ":triplanar", dbValue( terrain->triplanarSharpness ) );
			updateTerrain.bind( ":heightBlend", dbValue( terrain->heightBlend ) );
			updateTerrain.bind( ":farScale", dbValue( terrain->farTextureScale ) );
			updateTerrain.bind( ":farStart", dbValue( terrain->farBlendStart ) );
			updateTerrain.bind( ":farEnd", dbValue( terrain->farBlendEnd ) );
			updateTerrain.bind( ":displacementStart", dbValue( terrain->displacementStart ) );
			updateTerrain.bind( ":displacementEnd", dbValue( terrain->displacementEnd ) );
			updateTerrain.bind( ":microShadows", dbValue( terrain->microShadows ) );
			updateTerrain.bind( ":id", terrain->id );
			updateTerrain.exec();

			// Эрозия: строка TerrainErosion по id
			if( terrain->erosion && terrain->erosion->id )
			{
				std::string assignments = "seed = :seed";
				for( const auto& [column, field] : erosionFloatColumns )
					assignments += std::string( ", " ) + column + " = :" + column;
				for( const auto& [column, field] : erosionIntColumns )
					assignments += std::string( ", " ) + column + " = :" + column;
				SQLite::Statement updateErosion( db, "UPDATE TerrainErosion SET " + assignments + " WHERE id = :id" );
				updateErosion.bind( ":seed", static_cast<int64_t>( terrain->erosion->seed ) );
				for( const auto& [column, field] : erosionFloatColumns )
					updateErosion.bind( std::string( ":" ) + column, dbValue( ( *terrain->erosion ).*field ) );
				for( const auto& [column, field] : erosionIntColumns )
					updateErosion.bind( std::string( ":" ) + column, ( *terrain->erosion ).*field );
				updateErosion.bind( ":id", terrain->erosion->id );
				updateErosion.exec();
			}

			// Правила слоёв: строка TerrainLayerRules террейна (нет — появится)
			if( db.tableExists( "TerrainLayerRules" ) )
			{
				// Колонки — по таблице полей правил (GS::terrainLayerRuleFields)
				std::string columns = "terrain";
				std::string values = ":terrain";
				for( const GS::TerrainLayerRuleField& field : GS::terrainLayerRuleFields() )
				{
					const std::string column = field.column;
					if( field.range )
					{
						columns += ", " + column + "_from, " + column + "_to";
						values += ", :" + column + "_from, :" + column + "_to";
					}
					else
					{
						columns += ", " + column;
						values += ", :" + column;
					}
				}
				SQLite::Statement updateRules( db, "INSERT OR REPLACE INTO TerrainLayerRules (" + columns + ") VALUES (" + values + ")" );
				updateRules.bind( ":terrain", terrain->id );
				for( const GS::TerrainLayerRuleField& field : GS::terrainLayerRuleFields() )
				{
					const std::string column = field.column;
					if( field.range )
					{
						updateRules.bind( ":" + column + "_from", dbValue( ( terrain->layerRules.*field.range ).from ) );
						updateRules.bind( ":" + column + "_to", dbValue( ( terrain->layerRules.*field.range ).to ) );
					}
					else
						updateRules.bind( ":" + column, dbValue( terrain->layerRules.*field.value ) );
				}
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

		SQLite::Statement updateSet( db, "UPDATE ScatterSets SET shadow_lod_scale = :shadowLodScale WHERE id = :id" );
		for( const GS::ScatterSetSettings& set : scatterSets )
		{
			if( set.id == 0 )
				continue;
			updateSet.bind( ":shadowLodScale", dbValue( set.shadowLodScale ) );
			updateSet.bind( ":id", set.id );
			updateSet.exec();
			updateSet.reset();
		}

		SQLite::Statement updateScatter( db, "UPDATE ScatterLayers SET cell_size = :cellSize, near_border = :nearBorder, "
											 "far_border = :farBorder, near_fade = :nearFade, far_fade = :farFade, "
											 "size_multiplier = :size, size_variation = :sizeVariation, ground_color = :groundColor, "
											 "ground_color_height = :groundColorHeight, ground_color_far = :groundColorFar, "
											 "color_variation = :colorVariation, edge_spread = :edgeSpread, thinning_start = :thinStart, "
											 "thinning = :thinning, dither_fade = :ditherFade, jitter = :jitter, rotation_x = :rotationX, "
											 "rotation_y = :rotationY, rotation_z = :rotationZ, align_to_terrain = :align, "
											 "cast_shadow = :castShadow, shadow_impostor_distance = :shadowImpostor, "
											 "impostor_cast_shadow = :impostorShadow, impostor_base_shade = :baseShade, impostor_distance = :impostorDistance "
											 "WHERE id = :id" );
		SQLite::Statement updateScatterModel( db, "UPDATE ScatterLayerModels SET weight = :weight, cast_shadow = :castShadow, "
												  "lod_distances = :lods WHERE id = :id" );
		for( const GS::ScatterLayerRecord& record : scatterLayers )
		{
			const GS::ScatterLayerSettings& layer = record.settings;
			if( layer.id == 0 )
				continue;
			updateScatter.bind( ":cellSize", dbValue( layer.cellSize ) );
			updateScatter.bind( ":nearBorder", dbValue( layer.nearBorder ) );
			updateScatter.bind( ":farBorder", dbValue( layer.farBorder ) );
			updateScatter.bind( ":nearFade", dbValue( layer.nearFade ) );
			updateScatter.bind( ":farFade", dbValue( layer.farFade ) );
			updateScatter.bind( ":size", dbValue( layer.sizeMultiplier ) );
			updateScatter.bind( ":sizeVariation", dbValue( layer.sizeVariation ) );
			updateScatter.bind( ":groundColor", dbValue( layer.groundColor ) );
			updateScatter.bind( ":groundColorHeight", dbValue( layer.groundColorHeight ) );
			updateScatter.bind( ":groundColorFar", dbValue( layer.groundColorFar ) );
			updateScatter.bind( ":colorVariation", dbValue( layer.colorVariation ) );
			updateScatter.bind( ":edgeSpread", dbValue( layer.edgeSpread ) );
			updateScatter.bind( ":thinStart", dbValue( layer.thinStart ) );
			updateScatter.bind( ":thinning", dbValue( layer.thinning ) );
			updateScatter.bind( ":ditherFade", layer.ditherFade ? 1 : 0 );
			updateScatter.bind( ":jitter", dbValue( layer.jitter ) );
			updateScatter.bind( ":rotationX", dbValue( layer.rotationRange.x ) );
			updateScatter.bind( ":rotationY", dbValue( layer.rotationRange.y ) );
			updateScatter.bind( ":rotationZ", dbValue( layer.rotationRange.z ) );
			updateScatter.bind( ":align", layer.alignToTerrain ? 1 : 0 );
			updateScatter.bind( ":castShadow", layer.castShadow ? 1 : 0 );
			updateScatter.bind( ":shadowImpostor", dbValue( layer.shadowImpostorDistance ) );
			updateScatter.bind( ":impostorShadow", layer.impostorCastShadow ? 1 : 0 );
			updateScatter.bind( ":baseShade", dbValue( layer.impostorBaseShade ) );
			updateScatter.bind( ":impostorDistance", dbValue( layer.impostorDistance ) );
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
				updateScatterModel.bind( ":id", model.id );
				updateScatterModel.exec();
				updateScatterModel.reset();
			}
		}

		if( water && level.waterSimulationId )
		{
			SQLite::Statement updateWater( db, "UPDATE WaterSimulation SET time_scale = :timeScale, source_rate = :sourceRate, "
											   "flow_start = :flowStart, flow_full = :flowFull, source_radius = :sourceRadius, "
											   "rain = :rain, evaporation = :evaporation, manning = :manning, "
											   "absorption = :absorption, scatter_color = :scatterColor, "
											   "scatter_strength = :scatterStrength, roughness = :roughness, "
											   "ripple_scale = :rippleScale, ripple_strength = :rippleStrength, "
											   "calm_ripple = :calmRipple, refraction = :refraction, ripple_stretch = :rippleStretch, "
											   "foam_speed = :foamSpeed, foam_shear = :foamShear, detail_distance = :detailDistance, flow_settle_time = :flowSettleTime, "
											   "flow_time_step = :flowTimeStep, flow_viscosity = :flowViscosity, "
											   "flow_manning = :flowManning, flow_iterations = :flowIterations, flow_layer = :flowLayer, "
											   "flow_detail_settle_time = :flowDetailSettleTime WHERE id = :id" );
			updateWater.bind( ":timeScale", dbValue( water->timeScale ) );
			updateWater.bind( ":sourceRate", dbValue( water->sourceRate ) );
			updateWater.bind( ":flowStart", dbValue( water->flowStart ) );
			updateWater.bind( ":flowFull", dbValue( water->flowFull ) );
			updateWater.bind( ":sourceRadius", dbValue( water->sourceRadius ) );
			updateWater.bind( ":rain", dbValue( water->rain ) );
			updateWater.bind( ":evaporation", dbValue( water->evaporation ) );
			updateWater.bind( ":manning", dbValue( water->manning ) );
			updateWater.bind( ":absorption", vec3ToStr( water->absorption ) );
			updateWater.bind( ":scatterColor", vec3ToStr( water->scatterColor ) );
			updateWater.bind( ":scatterStrength", dbValue( water->scatterStrength ) );
			updateWater.bind( ":roughness", dbValue( water->roughness ) );
			updateWater.bind( ":rippleScale", dbValue( water->rippleScale ) );
			updateWater.bind( ":rippleStrength", dbValue( water->rippleStrength ) );
			updateWater.bind( ":calmRipple", dbValue( water->calmRipple ) );
			updateWater.bind( ":refraction", dbValue( water->refraction ) );
			updateWater.bind( ":rippleStretch", dbValue( water->rippleStretch ) );
			updateWater.bind( ":foamSpeed", dbValue( water->foamSpeed ) );
			updateWater.bind( ":foamShear", dbValue( water->foamShear ) );
			updateWater.bind( ":detailDistance", dbValue( water->detailDistance ) );
			updateWater.bind( ":flowSettleTime", dbValue( water->flow.settleTime ) );
			updateWater.bind( ":flowTimeStep", dbValue( water->flow.timeStep ) );
			updateWater.bind( ":flowViscosity", dbValue( water->flow.viscosity ) );
			updateWater.bind( ":flowManning", dbValue( water->flow.manning ) );
			updateWater.bind( ":flowIterations", water->flow.iterations );
			updateWater.bind( ":flowLayer", dbValue( water->flow.layer ) );
			updateWater.bind( ":flowDetailSettleTime", dbValue( water->flow.detailSettleTime ) );
			updateWater.bind( ":id", *level.waterSimulationId );
			updateWater.exec();

			// Русла конвейера: строка на строку WaterSimulation (нет — создаётся)
			std::string columns = "water_simulation, smooth";
			std::string values = ":id, :smooth";
			for( const auto& [column, field] : channelColumns )
			{
				columns += std::string( ", " ) + column;
				values += std::string( ", :" ) + column;
			}
			SQLite::Statement saveChannels( db, "INSERT OR REPLACE INTO WaterChannels (" + columns + ") VALUES (" + values + ")" );
			saveChannels.bind( ":id", *level.waterSimulationId );
			saveChannels.bind( ":smooth", water->channels.smooth );
			for( const auto& [column, field] : channelColumns )
				saveChannels.bind( std::string( ":" ) + column, dbValue( water->channels.*field ) );
			saveChannels.exec();

			// Кривые русел: включение, правка и множители (точки правит Tools/stream_edit.py)
			if( db.tableExists( "Streams" ) )
			{
				std::string assignments = "enabled = :enabled, edited = :edited";
				for( const auto& [column, field] : streamScaleColumns )
					assignments += std::string( ", " ) + column + " = :" + column;
				SQLite::Statement updateStream( db, "UPDATE Streams SET " + assignments + " WHERE id = :id" );
				for( const GS::StreamCurve& curve : water->streams )
				{
					if( curve.id == 0 )
						continue;
					updateStream.bind( ":enabled", curve.enabled ? 1 : 0 );
					updateStream.bind( ":edited", curve.edited ? 1 : 0 );
					for( const auto& [column, field] : streamScaleColumns )
						updateStream.bind( std::string( ":" ) + column, dbValue( curve.*field ) );
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
		SQLite::Statement updateEmitter( db, "UPDATE ParticleEmitters SET radius = :radius, height_min = :heightMin, "
											 "height_max = :heightMax, rate = :rate, lifetime_min = :lifetimeMin, "
											 "lifetime_max = :lifetimeMax, size_start = :sizeStart, size_end = :sizeEnd, "
											 "color = :color, alpha = :alpha, fade_in = :fadeIn, fade_out = :fadeOut, "
											 "velocity = :velocity, velocity_spread = :velocitySpread, gravity = :gravity, "
											 "drag = :drag, wind = :wind, curl = :curl, curl_scale = :curlScale, "
											 "water_flow = :waterFlow, water_speed = :waterSpeed, collide = :collide, "
											 "transmission = :transmission, emissive = :emissive WHERE id = :id" );
		for( const GS::ParticleEmitterSettings& emitter : particleEmitters )
		{
			if( emitter.id == 0 )
				continue;
			updateEmitter.bind( ":radius", dbValue( emitter.radius ) );
			updateEmitter.bind( ":heightMin", dbValue( emitter.heightMin ) );
			updateEmitter.bind( ":heightMax", dbValue( emitter.heightMax ) );
			updateEmitter.bind( ":rate", dbValue( emitter.rate ) );
			updateEmitter.bind( ":lifetimeMin", dbValue( emitter.lifetimeMin ) );
			updateEmitter.bind( ":lifetimeMax", dbValue( emitter.lifetimeMax ) );
			updateEmitter.bind( ":sizeStart", dbValue( emitter.sizeStart ) );
			updateEmitter.bind( ":sizeEnd", dbValue( emitter.sizeEnd ) );
			updateEmitter.bind( ":color", vec3ToStr( emitter.color ) );
			updateEmitter.bind( ":alpha", dbValue( emitter.alpha ) );
			updateEmitter.bind( ":fadeIn", dbValue( emitter.fadeIn ) );
			updateEmitter.bind( ":fadeOut", dbValue( emitter.fadeOut ) );
			updateEmitter.bind( ":velocity", vec3ToStr( emitter.velocity ) );
			updateEmitter.bind( ":velocitySpread", dbValue( emitter.velocitySpread ) );
			updateEmitter.bind( ":gravity", dbValue( emitter.gravity ) );
			updateEmitter.bind( ":drag", dbValue( emitter.drag ) );
			updateEmitter.bind( ":wind", dbValue( emitter.wind ) );
			updateEmitter.bind( ":curl", dbValue( emitter.curl ) );
			updateEmitter.bind( ":curlScale", dbValue( emitter.curlScale ) );
			updateEmitter.bind( ":waterFlow", dbValue( emitter.waterFlow ) );
			updateEmitter.bind( ":waterSpeed", dbValue( emitter.waterSpeed ) );
			updateEmitter.bind( ":collide", emitter.collide ? 1 : 0 );
			updateEmitter.bind( ":transmission", dbValue( emitter.transmission ) );
			updateEmitter.bind( ":emissive", dbValue( emitter.emissive ) );
			updateEmitter.bind( ":id", emitter.id );
			updateEmitter.exec();
			updateEmitter.reset();
		}
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
