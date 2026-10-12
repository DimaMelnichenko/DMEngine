#include "LevelSettings.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

using namespace DirectX;

namespace GS
{

const std::vector<TerrainLayerRuleField>& terrainLayerRuleFields()
{
	using R = TerrainLayerRules;
	auto range = []( const char* column, const char* group, const char* name, R::Range R::* field, float low, float high,
					 const char* unit, const char* tooltip )
	{
		return TerrainLayerRuleField{ column, group, name, field, nullptr, low, high, unit, tooltip };
	};
	auto value = []( const char* column, const char* group, const char* name, float R::* field, float low, float high,
					 const char* unit, const char* tooltip )
	{
		return TerrainLayerRuleField{ column, group, name, nullptr, field, low, high, unit, tooltip };
	};
	static const char* rock = "Rock and snow";
	static const char* stones = "Stones";
	static const char* grass = "Grass and flowers";
	static const char* forest = "Forest";
	static const char* shrubs = "Shrubs";
	static const char* wind = "Wind";
	static const char* shape = "Shape";
	static const std::vector<TerrainLayerRuleField> fields = {
		range( "rock_slope", rock, "Rock slope", &R::rockSlope, 0.0f, 90.0f, "deg", "Rock on steep slopes: from, to" ),
		value( "rock_noise", rock, "Rock slope noise", &R::rockNoise, 0.0f, 30.0f, "deg", "Noise added to the slope of the rock rule" ),
		range( "bedrock_wear", rock, "Bedrock wear", &R::bedrockWear, 0.0f, 10.0f, "", "Rock where the erosion wore the ground away: from, to" ),
		range( "bedrock_slope", rock, "Bedrock slope", &R::bedrockSlope, 0.0f, 90.0f, "deg", "Worn bedrock only on slopes: from, to" ),
		value( "bedrock_share", rock, "Bedrock share", &R::bedrockShare, 0.0f, 1.0f, "", "Weight of the worn bedrock rock" ),
		range( "snow_height", rock, "Snow height", &R::snowHeight, 0.0f, 1.0f, "", "Snow above, share of the height multiplier: from, to" ),
		value( "snow_north", rock, "Snow north", &R::snowNorth, 0.0f, 0.3f, "", "Snow lower on north slopes, share of the height multiplier" ),
		value( "snow_noise", rock, "Snow height noise", &R::snowNoise, 0.0f, 0.3f, "", "Noise added to the height of the snow rule, share of the height multiplier" ),
		value( "rock_clears_snow", rock, "Rock clears snow", &R::rockClearsSnow, 0.0f, 1.0f, "", "Share of the snow removed on rock" ),
		range( "talus_map", stones, "Talus map", &R::talusMap, 0.0f, 2.0f, "", "Scree of the erosion talus map: from, to" ),
		range( "boulder_slope", stones, "Boulder slope", &R::boulderSlope, 0.0f, 90.0f, "deg", "Stones on steepish slopes: from, to" ),
		value( "boulder_noise", stones, "Boulder slope noise", &R::boulderNoise, 0.0f, 30.0f, "deg", "Noise added to the slope of the stones rule" ),
		value( "boulder_share", stones, "Boulder share", &R::boulderShare, 0.0f, 1.0f, "", "Weight of the stones on steepish slopes" ),
		range( "gully_flow", stones, "Gully flow", &R::gullyFlow, 0.0f, 8.0f, "", "Gravel in gullies: log10 of the catchment, m2: from, to" ),
		range( "gully_slope", stones, "Gully slope", &R::gullySlope, 0.0f, 90.0f, "deg", "Gully gravel only on slopes: from, to" ),
		value( "gully_share", stones, "Gully share", &R::gullyShare, 0.0f, 1.0f, "", "Weight of the gully gravel" ),
		range( "pebble_patches", stones, "Pebble patches", &R::pebblePatches, 0.0f, 1.0f, "", "Pebble patches by noise 0..1: from, to" ),
		range( "pebble_patch_slope", stones, "Pebble patch slope", &R::pebblePatchSlope, 0.0f, 90.0f, "deg", "Pebble patches only on slopes: from, to" ),
		value( "pebble_patch_share", stones, "Pebble patch share", &R::pebblePatchShare, 0.0f, 1.0f, "", "Weight of the pebble patches" ),
		range( "moisture_twi", grass, "Moisture TWI", &R::moistureTwi, 0.0f, 20.0f, "", "Wet ground by the topographic wetness index ln(a / tan b): from, to" ),
		range( "wet_grass", grass, "Wet grass", &R::wetGrass, 0.0f, 1.0f, "", "Lush grass by moisture and noise 0..1: from, to" ),
		value( "wet_grass_moisture", grass, "Wet grass moisture", &R::wetGrassMoisture, 0.0f, 1.0f, "", "Share of the moisture in the wet grass rule, the rest is noise" ),
		range( "flowers", grass, "Flowers", &R::flowers, 0.0f, 1.0f, "", "Flowers (mask_camomile) by noise 0..1: from, to" ),
		value( "flowers_moisture", grass, "Flowers moisture", &R::flowersMoisture, 0.0f, 1.0f, "", "Moisture added to the flower noise" ),
		range( "treeline", forest, "Treeline", &R::treeline, 0.0f, 1.0f, "", "Forest ends higher, share of the height multiplier: from, to" ),
		value( "treeline_noise", forest, "Treeline noise", &R::treelineNoise, 0.0f, 0.3f, "", "Noise added to the height of the treeline, share of the height multiplier" ),
		value( "treeline_north", forest, "Treeline north", &R::treelineNorth, 0.0f, 0.3f, "", "Treeline lower on north slopes, share of the height multiplier" ),
		range( "forest_slope", forest, "Forest slope", &R::forestSlope, 0.0f, 90.0f, "deg", "No forest steeper: from, to" ),
		value( "forest_slope_noise", forest, "Forest slope noise", &R::forestSlopeNoise, 0.0f, 30.0f, "deg", "Noise added to the slope of the forest rule" ),
		value( "stones_clear_forest", forest, "Stones clear forest", &R::stonesClearForest, 0.0f, 1.0f, "", "Share of the forest removed on scree and stones" ),
		range( "valley_floor_height", forest, "Valley floor height", &R::valleyFloorHeight, 0.0f, 1.0f, "", "Meadows of the valley floor lower: from, to" ),
		range( "valley_floor_slope", forest, "Valley floor slope", &R::valleyFloorSlope, 0.0f, 90.0f, "deg", "Valley floor only on flat ground: from, to" ),
		range( "clearings", forest, "Clearings", &R::clearings, 0.0f, 1.0f, "", "Clearings where the noise 0..1 is lower: from, to" ),
		value( "valley_clearings", forest, "Valley clearings", &R::valleyClearings, 0.0f, 1.0f, "", "More clearings on the valley floor" ),
		range( "spruce_height", forest, "Spruce height", &R::spruceHeight, 0.0f, 1.0f, "", "Spruce higher, share of the height multiplier: from, to" ),
		range( "pine_slope", forest, "Pine slope", &R::pineSlope, 0.0f, 90.0f, "deg", "Pine on dry middle slopes: from, to" ),
		range( "pine_height", forest, "Pine height", &R::pineHeight, 0.0f, 1.0f, "", "Pine lower, share of the height multiplier: from, to" ),
		range( "birch_moisture", forest, "Birch moisture", &R::birchMoisture, 0.0f, 1.0f, "", "Birch in wet hollows: from, to" ),
		range( "birch_height", forest, "Birch height", &R::birchHeight, 0.0f, 1.0f, "", "Birch lower, share of the height multiplier: from, to" ),
		range( "birch_slope", forest, "Birch slope", &R::birchSlope, 0.0f, 90.0f, "deg", "Birch on gentle slopes: from, to" ),
		value( "stand_contrast", forest, "Stand contrast", &R::standContrast, 1.0f, 8.0f, "", "Power of the tree type shares: higher - purer stands, narrower mixed bands" ),
		value( "bank_width", shrubs, "Bank width", &R::bankWidth, 0.0f, 10.0f, "texels", "Bank strip wider than the channel" ),
		range( "riparian_density", shrubs, "Riparian density", &R::riparianDensity, 0.0f, 1.0f, "", "Shrubs by the streams in patches by noise 0..1: from, to" ),
		range( "riparian_grass", shrubs, "Riparian grass", &R::riparianGrass, 0.0f, 1.0f, "", "Tall grass by the water, thinned by noise 0..1: from, to" ),
		range( "riparian_height", shrubs, "Riparian height", &R::riparianHeight, 0.0f, 1.0f, "", "Shrubs and tall grass by the water lower: from, to" ),
		range( "krummholz_height", shrubs, "Krummholz height", &R::krummholzHeight, 0.0f, 1.0f, "", "Krummholz above the treeline: from, to" ),
		range( "krummholz_top", shrubs, "Krummholz top", &R::krummholzTop, 0.0f, 1.0f, "", "Krummholz ends higher: from, to" ),
		range( "dry_gaps_slope", shrubs, "Dry gaps slope", &R::dryGapsSlope, 0.0f, 90.0f, "deg", "Shrubs in the gaps of dry slopes: from, to" ),
		value( "dry_gaps_share", shrubs, "Dry gaps share", &R::dryGapsShare, 0.0f, 1.0f, "", "Weight of the shrubs in the gaps of dry slopes" ),
		value( "wind_reach", wind, "Wind reach", &R::windReach, 10.0f, 1000.0f, "m", "The horizon against the prevailing wind (Wind of the level) is searched this far" ),
		range( "wind_open", wind, "Wind open", &R::windOpen, -30.0f, 30.0f, "deg", "Open to the wind where the horizon angle against the wind falls: from, to" ),
		range( "wind_shelter", wind, "Wind shelter", &R::windShelter, -30.0f, 30.0f, "deg", "Sheltered where the horizon angle against the wind rises: from, to" ),
		value( "ridge_radius", wind, "Ridge radius", &R::ridgeRadius, 2.0f, 200.0f, "m", "Ridges and hollows - against the ground smoothed this wide" ),
		range( "ridge", wind, "Ridge", &R::ridge, -10.0f, 20.0f, "m", "Ridge - above the smoothed ground: from, to" ),
		range( "hollow", wind, "Hollow", &R::hollow, -20.0f, 10.0f, "m", "Hollow - below the smoothed ground: from, to" ),
		value( "snow_scour", wind, "Snow scour", &R::snowScour, 0.0f, 0.3f, "", "Snow higher where it is open to the wind (blown away), share of the height multiplier" ),
		value( "snow_drift", wind, "Snow drift", &R::snowDrift, 0.0f, 0.3f, "", "Snow lower in the lee and in hollows (drifts), share of the height multiplier" ),
		value( "wind_treeline", wind, "Wind treeline", &R::windTreeline, 0.0f, 0.3f, "", "Treeline lower on ridges open to the wind, share of the height multiplier" ),
		value( "wind_thins_forest", wind, "Wind thins forest", &R::windThinsForest, 0.0f, 1.0f, "", "Forest sparser and smaller on ridges open to the wind" ),
		value( "slope_blur", shape, "Slope blur", &R::slopeBlur, 0.0f, 16.0f, "texels", "Smoothing of the slope and north facing" ),
		value( "moisture_blur", shape, "Moisture blur", &R::moistureBlur, 0.0f, 16.0f, "texels", "Smoothing of the moisture" ),
		value( "erosion_blur", shape, "Erosion blur", &R::erosionBlur, 0.0f, 16.0f, "texels", "Smoothing of the erosion maps (wear, talus)" ),
		value( "layer_noise_scale", shape, "Layer noise scale", &R::layerNoiseScale, 0.1f, 8.0f, "", "Wavelengths of the rock, snow and stones noise" ),
		value( "meadow_noise_scale", shape, "Meadow noise scale", &R::meadowNoiseScale, 0.1f, 8.0f, "", "Wavelengths of the two grasses and flowers noise" ),
		value( "forest_noise_scale", shape, "Forest noise scale", &R::forestNoiseScale, 0.1f, 8.0f, "", "Wavelengths of the treeline, clearings, stands and shrubs noise" ),
	};
	return fields;
}

Tonemapper tonemapperFromName( const std::string& name )
{
	return name == "None" ? Tonemapper::none : name == "ACES" ? Tonemapper::aces : Tonemapper::agx;
}

const char* tonemapperName( Tonemapper tonemapper )
{
	switch( tonemapper )
	{
		case Tonemapper::none: return "None";
		case Tonemapper::aces: return "ACES";
		default: return "AgX";
	}
}

MeteringMode meteringModeFromName( const std::string& name )
{
	return name == "Manual" ? MeteringMode::manual : MeteringMode::autoHistogram;
}

const char* meteringModeName( MeteringMode mode )
{
	return mode == MeteringMode::manual ? "Manual" : "AutoHistogram";
}

std::vector<XMFLOAT2> curveFromText( const std::string& text )
{
	std::vector<XMFLOAT2> curve;
	std::istringstream keys( text );
	std::string key;
	while( std::getline( keys, key, ';' ) )
	{
		XMFLOAT2 value;
		if( std::sscanf( key.c_str(), " %f , %f", &value.x, &value.y ) == 2 )
			curve.push_back( value );
	}
	std::sort( curve.begin(), curve.end(), []( const XMFLOAT2& a, const XMFLOAT2& b ) { return a.x < b.x; } );
	if( curve.size() > maxExposureCurveKeys )
		curve.resize( maxExposureCurveKeys );
	return curve;
}

std::string curveText( const std::vector<XMFLOAT2>& curve )
{
	std::string text;
	for( const XMFLOAT2& key : curve )
	{
		char value[64];
		std::snprintf( value, sizeof( value ), "%s%g,%g", text.empty() ? "" : "; ", key.x, key.y );
		text += value;
	}
	return text;
}

}
