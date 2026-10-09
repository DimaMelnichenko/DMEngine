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
	{ "ribbon_overlap", &GS::WaterChannelsSettings::ribbonOverlap },
	{ "freeboard", &GS::WaterChannelsSettings::freeboard },
	{ "lake_depth_ratio", &GS::WaterChannelsSettings::lakeDepthRatio },
	{ "lake_min_depth", &GS::WaterChannelsSettings::lakeMinDepth },
	{ "lake_max_depth", &GS::WaterChannelsSettings::lakeMaxDepth },
	{ "lake_shelf_width", &GS::WaterChannelsSettings::lakeShelfWidth },
	{ "lake_shelf_depth", &GS::WaterChannelsSettings::lakeShelfDepth },
	{ "lake_drop_slope", &GS::WaterChannelsSettings::lakeDropSlope },
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
	if( !query.getColumn( "paint_layer" ).isNull() )
		channels.paintLayer = query.getColumn( "paint_layer" ).getInt();
}

// Объекты сцены уровня: правки из GUI («Save level») — строки LevelModels, Terrain и TerrainLayers, ScatterLayers и ScatterLayerModels,
// WaterSimulation, ParticleEmitters. Только колонки, которые правятся в окнах; состав (какие слои, модели, эмиттеры)
// не меняется

bool LibraryLoader::saveLevelScene( const LevelDescription& level, const std::vector<LevelDescription::ModelInstance>& models,
									const std::optional<GS::TerrainSettings>& terrain,
									const std::vector<GS::ScatterLayerRecord>& scatterLayers,
									const std::optional<GS::WaterSimulationSettings>& water,
									const std::vector<GS::ParticleEmitterSettings>& particleEmitters )
{
	try
	{
		SQLite::Database& db = DBConnector::instance().db();
		SQLite::Transaction transaction( db );

		SQLite::Statement updateModel( db, "UPDATE LevelModels SET position = :position, rotation = :rotation, scale = :scale WHERE id = :id" );
		for( const LevelDescription::ModelInstance& model : models )
		{
			if( model.id == 0 )
				continue;
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
												 "far_texture_scale = :farScale, far_blend_start = :farStart, far_blend_end = :farEnd "
												 "WHERE id = :id" );
			updateTerrain.bind( ":heightMultiplier", dbValue( terrain->heightMultiplier ) );
			updateTerrain.bind( ":triplanar", dbValue( terrain->triplanarSharpness ) );
			updateTerrain.bind( ":heightBlend", dbValue( terrain->heightBlend ) );
			updateTerrain.bind( ":farScale", dbValue( terrain->farTextureScale ) );
			updateTerrain.bind( ":farStart", dbValue( terrain->farBlendStart ) );
			updateTerrain.bind( ":farEnd", dbValue( terrain->farBlendEnd ) );
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

			SQLite::Statement updateLayer( db, "UPDATE TerrainLayers SET tiling = :tiling WHERE terrain = :terrain AND layer = :layer" );
			for( uint32_t layer = 0; layer < terrain->layerTiling.size(); ++layer )
			{
				if( terrain->layerTiling[layer] <= 0.0f )
					continue;
				updateLayer.bind( ":tiling", dbValue( terrain->layerTiling[layer] ) );
				updateLayer.bind( ":terrain", terrain->id );
				updateLayer.bind( ":layer", layer );
				updateLayer.exec();
				updateLayer.reset();
			}
		}

		SQLite::Statement updateScatter( db, "UPDATE ScatterLayers SET cell_size = :cellSize, near_border = :nearBorder, "
											 "far_border = :farBorder, near_fade = :nearFade, far_fade = :farFade, "
											 "size_multiplier = :size, jitter = :jitter, rotation_x = :rotationX, "
											 "rotation_y = :rotationY, rotation_z = :rotationZ, align_to_terrain = :align, "
											 "cast_shadow = :castShadow, shadow_impostor_distance = :shadowImpostor WHERE id = :id" );
		SQLite::Statement updateScatterModel( db, "UPDATE ScatterLayerModels SET weight = :weight, cast_shadow = :castShadow WHERE id = :id" );
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
			updateScatter.bind( ":jitter", dbValue( layer.jitter ) );
			updateScatter.bind( ":rotationX", dbValue( layer.rotationRange.x ) );
			updateScatter.bind( ":rotationY", dbValue( layer.rotationRange.y ) );
			updateScatter.bind( ":rotationZ", dbValue( layer.rotationRange.z ) );
			updateScatter.bind( ":align", layer.alignToTerrain ? 1 : 0 );
			updateScatter.bind( ":castShadow", layer.castShadow ? 1 : 0 );
			updateScatter.bind( ":shadowImpostor", dbValue( layer.shadowImpostorDistance ) );
			updateScatter.bind( ":id", layer.id );
			updateScatter.exec();
			updateScatter.reset();

			for( const GS::ScatterModelSettings& model : record.models )
			{
				if( model.id == 0 )
					continue;
				updateScatterModel.bind( ":weight", dbValue( model.weight ) );
				updateScatterModel.bind( ":castShadow", model.castShadow ? 1 : 0 );
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
											   "calm_ripple = :calmRipple, refraction = :refraction, flow_period = :flowPeriod, "
											   "foam_speed = :foamSpeed, foam_shear = :foamShear WHERE id = :id" );
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
			updateWater.bind( ":flowPeriod", dbValue( water->flowPeriod ) );
			updateWater.bind( ":foamSpeed", dbValue( water->foamSpeed ) );
			updateWater.bind( ":foamShear", dbValue( water->foamShear ) );
			updateWater.bind( ":id", *level.waterSimulationId );
			updateWater.exec();

			// Русла конвейера: строка на строку WaterSimulation (нет — создаётся)
			std::string columns = "water_simulation, smooth, paint_layer";
			std::string values = ":id, :smooth, :paintLayer";
			for( const auto& [column, field] : channelColumns )
			{
				columns += std::string( ", " ) + column;
				values += std::string( ", :" ) + column;
			}
			SQLite::Statement saveChannels( db, "INSERT OR REPLACE INTO WaterChannels (" + columns + ") VALUES (" + values + ")" );
			saveChannels.bind( ":id", *level.waterSimulationId );
			saveChannels.bind( ":smooth", water->channels.smooth );
			saveChannels.bind( ":paintLayer", water->channels.paintLayer );
			for( const auto& [column, field] : channelColumns )
				saveChannels.bind( std::string( ":" ) + column, dbValue( water->channels.*field ) );
			saveChannels.exec();
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

		transaction.commit();
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Save level scene: " ) + e.what() );
		return false;
	}
	return true;
}
