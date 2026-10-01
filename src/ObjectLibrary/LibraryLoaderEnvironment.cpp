#include "LibraryLoader.h"
#include <cstdio>
#include <cstdlib>
#include "DBConnector.h"
#include "Logger\Logger.h"
#include "Utils\utilites.h"

// Свет и окружение уровня: строки LevelLights, SunPosition, SkyAtmosphere, Wind, HDRIBackdrop, PostProcessSettings —
// загрузка в LevelDescription и сохранение правок из GUI («Save level environment»)

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
		light.setAtmosphereSunLightIndex( query.getColumn( "atmosphere_sun_light_index" ).getInt() );

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
		GS::SunPositionSettings& settings = level.sunPosition.emplace();
		settings.latitude = value( "latitude" );
		settings.longitude = value( "longitude" );
		settings.timeZone = value( "time_zone" );
		settings.northOffset = value( "north_offset" );
		settings.timeOfDay = value( "time_of_day" );
		settings.timeScale = value( "time_scale" );
		// Дата — как в SQLite: YYYY-MM-DD
		const std::string date = query.getColumn( "date" ).getString();
		if( sscanf_s( date.c_str(), "%d-%d-%d", &settings.year, &settings.month, &settings.day ) != 3 )
			LOG( "Sun position " + std::to_string( *level.sunPositionId ) + ": wrong date '" + date + "', expected YYYY-MM-DD" );
	}

	if( level.atmosphereId )
	{
		SQLite::Statement query( dbConnect().db(), "SELECT sky_intensity, haze, ground_albedo, aerial_perspective_view_distance_scale, "
												   "night_sky_luminance FROM SkyAtmosphere WHERE id = :id" );
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
		level.atmosphere.nightSkyLuminance = static_cast<float>( query.getColumn( "night_sky_luminance" ).getDouble() );
	}

	if( level.windId )
	{
		SQLite::Statement query( dbConnect().db(), "SELECT direction, strength, speed, min_gust_amount, max_gust_amount, gust_size "
												   "FROM Wind WHERE id = :id" );
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
		wind.strength = static_cast<float>( query.getColumn( "strength" ).getDouble() );
		wind.speed = static_cast<float>( query.getColumn( "speed" ).getDouble() );
		wind.minGustAmount = static_cast<float>( query.getColumn( "min_gust_amount" ).getDouble() );
		wind.maxGustAmount = static_cast<float>( query.getColumn( "max_gust_amount" ).getDouble() );
		wind.gustSize = static_cast<float>( query.getColumn( "gust_size" ).getDouble() );
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
		GS::HDRIBackdropSettings& hdri = level.hdriBackdrop.emplace();
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
		GS::PostProcessSettings& settings = level.postProcess;
		settings.meteringMode = GS::meteringModeFromName( query.getColumn( "metering_mode" ).getString() );
		settings.manualEV100 = value( "manual_ev100" );
		settings.minEV100 = value( "min_ev100" );
		settings.maxEV100 = value( "max_ev100" );
		settings.histogramLowPercent = value( "histogram_low_percent" );
		settings.histogramHighPercent = value( "histogram_high_percent" );
		settings.speedUp = value( "speed_up" );
		settings.speedDown = value( "speed_down" );
		level.postProcess.exposureCompensation = static_cast<float>( query.getColumn( "exposure_compensation" ).getDouble() );
		level.postProcess.tonemapper = GS::tonemapperFromName( query.getColumn( "tonemapper" ).getString() );
		level.postProcess.bloomIntensity = static_cast<float>( query.getColumn( "bloom_intensity" ).getDouble() );
		level.postProcess.bloomThreshold = static_cast<float>( query.getColumn( "bloom_threshold" ).getDouble() );
		level.postProcess.purkinjeShift = value( "purkinje_shift" );
		const SQLite::Column curve = query.getColumn( "exposure_compensation_curve" );
		if( !curve.isNull() )
			level.postProcess.exposureCompensationCurve = GS::curveFromText( curve.getString() );
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
										  const std::optional<GS::SunPositionSettings>& sunPosition,
										  const std::optional<GS::SkyAtmosphereSettings>& atmosphere,
										  const std::optional<GS::HDRIBackdropSettings>& hdri,
										  const GS::PostProcessSettings& postProcess, const GS::WindSettings& wind )
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
											 "time_of_day = :timeOfDay, time_scale = :timeScale WHERE id = :id" );
			updateSun.bind( ":latitude", dbValue( sunPosition->latitude ) );
			updateSun.bind( ":longitude", dbValue( sunPosition->longitude ) );
			updateSun.bind( ":timeZone", dbValue( sunPosition->timeZone ) );
			updateSun.bind( ":northOffset", dbValue( sunPosition->northOffset ) );
			updateSun.bind( ":date", std::string( date ) );
			updateSun.bind( ":timeOfDay", dbValue( sunPosition->timeOfDay ) );
			updateSun.bind( ":timeScale", dbValue( sunPosition->timeScale ) );
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
													"ground_albedo = :albedo, aerial_perspective_view_distance_scale = :aerialScale, "
													"night_sky_luminance = :nightSky WHERE id = :id" );
			updateAtmosphere.bind( ":intensity", dbValue( atmosphere->skyIntensity ) );
			updateAtmosphere.bind( ":haze", dbValue( atmosphere->haze ) );
			updateAtmosphere.bind( ":albedo", dbValue( atmosphere->groundAlbedo ) );
			updateAtmosphere.bind( ":aerialScale", dbValue( atmosphere->aerialPerspectiveViewDistanceScale ) );
			updateAtmosphere.bind( ":nightSky", dbValue( atmosphere->nightSkyLuminance ) );
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
												 "speed_up = :speedUp, speed_down = :speedDown, purkinje_shift = :purkinjeShift, "
												 "exposure_compensation_curve = :curve WHERE id = :id" );
		updatePostProcess.bind( ":exposure", dbValue( postProcess.exposureCompensation ) );
		updatePostProcess.bind( ":tonemapper", GS::tonemapperName( postProcess.tonemapper ) );
		updatePostProcess.bind( ":bloomIntensity", dbValue( postProcess.bloomIntensity ) );
		updatePostProcess.bind( ":bloomThreshold", dbValue( postProcess.bloomThreshold ) );
		updatePostProcess.bind( ":meteringMode", GS::meteringModeName( postProcess.meteringMode ) );
		updatePostProcess.bind( ":manualEV100", dbValue( postProcess.manualEV100 ) );
		updatePostProcess.bind( ":minEV100", dbValue( postProcess.minEV100 ) );
		updatePostProcess.bind( ":maxEV100", dbValue( postProcess.maxEV100 ) );
		updatePostProcess.bind( ":lowPercent", dbValue( postProcess.histogramLowPercent ) );
		updatePostProcess.bind( ":highPercent", dbValue( postProcess.histogramHighPercent ) );
		updatePostProcess.bind( ":speedUp", dbValue( postProcess.speedUp ) );
		updatePostProcess.bind( ":speedDown", dbValue( postProcess.speedDown ) );
		updatePostProcess.bind( ":purkinjeShift", dbValue( postProcess.purkinjeShift ) );
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
		SQLite::Statement updateWind( db, "UPDATE Wind SET direction = :direction, strength = :strength, speed = :speed, "
										  "min_gust_amount = :minGust, max_gust_amount = :maxGust, gust_size = :gustSize WHERE id = :id" );
		updateWind.bind( ":direction", vec3ToStr( wind.direction ) );
		updateWind.bind( ":strength", dbValue( wind.strength ) );
		updateWind.bind( ":speed", dbValue( wind.speed ) );
		updateWind.bind( ":minGust", dbValue( wind.minGustAmount ) );
		updateWind.bind( ":maxGust", dbValue( wind.maxGustAmount ) );
		updateWind.bind( ":gustSize", dbValue( wind.gustSize ) );
		updateWind.bind( ":id", *level.windId );
		updateWind.exec();
		SQLite::Statement updateLevelWind( db, "UPDATE Levels SET wind = :wind WHERE id = :id" );
		updateLevelWind.bind( ":wind", *level.windId );
		updateLevelWind.bind( ":id", level.id );
		updateLevelWind.exec();

		transaction.commit();
	}
	catch( const std::exception& e )
	{
		LOG( std::string( "Can`t save level environment: " ) + e.what() );
		return false;
	}

	return true;
}
