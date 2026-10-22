#include "LevelSettings.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

using namespace DirectX;

namespace GS
{

const SettingsSchema<VolumetricCloudSettings>& volumetricCloudSchema()
{
	using Settings = VolumetricCloudSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "layer_bottom_altitude", "Layer bottom altitude", DM_SETTING( layerBottomAltitude ), 200.0f, 8000.0f, "m" },
		{ "layer_height", "Layer height", DM_SETTING( layerHeight ), 100.0f, 10000.0f, "m" },
		{ "coverage", "Coverage", DM_SETTING( coverage ), 0.0f, 1.0f },
		{ "density", "Density", DM_SETTING( density ), 0.0f, 0.2f, "1/m" },
		{ "albedo", "Albedo", DM_SETTING( albedo ), 0.0f, 0.0f, "", "", settingColor },
		{ "shape_scale", "Shape scale", DM_SETTING( shapeScale ), 1000.0f, 40000.0f, "m" },
		{ "detail_scale", "Detail scale", DM_SETTING( detailScale ), 100.0f, 5000.0f, "m" },
		{ "weather_scale", "Weather scale", DM_SETTING( weatherScale ), 5000.0f, 200000.0f, "m" },
		{ "wind_speed", "Wind speed", DM_SETTING( windSpeed ), 0.0f, 50.0f, "m/s" },
		{ "shadow_strength", "Shadow strength", DM_SETTING( shadowStrength ), 0.0f, 4.0f },
		{ "tracing_max_distance", "Tracing max distance", DM_SETTING( tracingMaxDistance ), 5000.0f, 200000.0f, "m" },
	},
	// Разделы окна
	{ { "layer_bottom_altitude", "Layer" }, { "albedo", "Lighting" }, { "shape_scale", "Shape" }, { "wind_speed", "Wind" }, { "shadow_strength", "Lighting" }, { "tracing_max_distance", "Quality" } } );
	return schema;
}

const SettingsSchema<ScatterSetSettings>& scatterSetSchema()
{
	using Settings = ScatterSetSettings;
	static const SettingsSchema<Settings> schema = {
		{ "shadow_lod_scale", "Shadow LOD scale", DM_SETTING( shadowLodScale ), 0.1f, 1.0f, "",
		  "LOD distances of the shadow views, times: less - the shadow from a coarser LOD (with the model Shadow LOD scale)" },
	};
	return schema;
}

const SettingsSchema<ScatterLayerSettings>& scatterLayerSchema()
{
	using Settings = ScatterLayerSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "cell_size", "Cell size", DM_SETTING( cellSize ), 0.05f, 20.0f, "m", "Grid step: at most one instance per cell",
		  settingLogarithmic | settingClampLow },
		{ "near_border", "Near border", DM_SETTING( nearBorder ), 0.0f, 200.0f, "m", "Ring around the camera where the layer grows",
		  settingClampLow },
		{ "far_border", "Far border", DM_SETTING( farBorder ), 1.0f, 200.0f, "m", "Ring around the camera where the layer grows" },
		{ "near_fade", "Near fade", DM_SETTING( nearFade ), 0.0f, 100.0f, "m", "Instances shrink to zero over this width at the near border",
		  settingClampLow },
		{ "far_fade", "Far fade", DM_SETTING( farFade ), 0.0f, 200.0f, "m", "Instances shrink to zero over this width at the far border",
		  settingClampLow },
		{ "size_multiplier", "Size", DM_SETTING( sizeMultiplier ), 0.05f, 10.0f, "", "Size multiplier of the models", settingLogarithmic },
		{ "size_variation", "Size variation", DM_SETTING( sizeVariation ), 0.0f, 1.0f, "",
		  "Random size spread: an instance is Size x (1 - variation ... 1)", settingClamp },
		{ "jitter", "Jitter", DM_SETTING( jitter ), 0.0f, 1.0f, "", "Random offset inside the cell, share of the cell size", settingClampLow },
		// Цвет земли у растения: трава на пожелтевшей земле не сочно-зелёная, вдали пучки не выделяются тёмными корнями
		{ "ground_color", "Ground color", DM_SETTING( groundColor ), 0.0f, 1.0f, "",
		  "Share of the ground color under the plant at its root (the terrain layers by weight, with their tint): 0 - own color", settingClamp },
		{ "ground_color_height", "Ground color height", DM_SETTING( groundColorHeight ), 0.0f, 2.0f, "m",
		  "Height above the root where the ground color fades out to the plant's own color", settingClampLow },
		{ "ground_color_far", "Ground color far", DM_SETTING( groundColorFar ), 0.0f, 1.0f, "",
		  "Share of the ground color over the whole plant at the far border (from half the ring): far plants blend into the ground",
		  settingClamp },
		{ "color_variation", "Color variation", DM_SETTING( colorVariation ), 0.0f, 0.5f, "",
		  "Random brightness spread between plants (+-); the ground brightness spots of the terrain apply too" },
		// Края кольца и прореживание вдали: граница не ровной дугой, вдали меньше экземпляров
		{ "edge_spread", "Edge spread", DM_SETTING( edgeSpread ), 0.0f, 1.0f, "",
		  "Own fade distance of each instance over the fade bands (and a coarse world noise): the ring edge is ragged, "
		  "plants vanish one by one; 0 - all shrink together", settingClamp },
		{ "thinning_start", "Thinning start", DM_SETTING( thinStart ), 0.0f, 1.0f, "m", "Distance where thinning starts", settingClampLow },
		{ "dither_fade", "Dither fade", DM_SETTING( ditherFade ), 0.0f, 0.0f, "",
		  "At the ring borders an instance fades in from transparency (a share of its pixels, like foliage in UE) instead of "
		  "growing from zero size; needs materials with dithered LOD transition" },
		{ "thinning", "Thinning", DM_SETTING( thinning ), 0.0f, 0.95f, "",
		  "Share of instances removed at the far border (the same ones always; the rest are bigger - the cover stays): fewer "
		  "instances far away; 0 - none", settingClamp },
		{ "water", "Water", DM_SETTING( water ), 0.0f, 0.0f, "",
		  "Where the layer grows at the water: up to the waterline; only above the overflow of the lakes (not in the shore strip "
		  "from the waterline to the overflow, Lake freeboard of Channels - the water floods it); shallows - the shore strip of the "
		  "lakes and the water up to Lake shallows depth (Layer rules, Water beds), as cattail", 0, "ground", { 0, 1, 2 },
		  { "Up to the waterline", "Above the lake overflow", "Shallows" } },
		{ "stand_size", "Stand size", DM_SETTING( standSize ), 0.0f, 50.0f, "m",
		  "Dense stands: the layer grows only in patches of world noise this big (cattail - thick clumps, not a strip); "
		  "0 - no stands, the mask decides", settingClamp, "ground" },
		{ "stand_cover", "Stand cover", DM_SETTING( standCover ), 0.0f, 1.0f, "",
		  "With Stand size: about this share of the area is covered by the stands", settingClamp, "ground" },
		// Фильтр по слоям земли (как Landscape Grass Type в UE): роли — флажки подокна «Grows on» у слоя, здесь — порог
		{ "layer_filter", nullptr, DM_SETTING( layerFilter ) },
		{ "layer_filter_threshold", "Ground threshold", DM_SETTING( layerFilterThreshold ), 0.0f, 0.5f, "",
		  "Share of the Grows on terrain layers where the density is half; from twice the share - full, at 0 - none "
		  "(a snow or pebble stroke over a meadow removes the grass); 0 - any share of them", settingClamp, "ground" },
		// Куртины: модели слоя — виды, растут пятнами (окно варианта: «Patch group» — модели одного вида в одних пятнах)
		{ "patch_scale", "Patch size", DM_SETTING( patchScale ), 0.0f, 100.0f, "m",
		  "Patches of the layer models (species): size of the world noise spots where a model group outnumbers the others; "
		  "0 - the models are mixed evenly by weight", settingClampLow, "placement" },
		{ "patch_strength", "Patch strength", DM_SETTING( patchStrength ), 0.0f, 1.0f, "",
		  "How strongly a model group outnumbers the others in its patches: weight x e^(4 x strength x noise)", settingClamp, "placement" },
		{ "rotation_x,rotation_y,rotation_z", "Rotation", DM_SETTING( rotationRange ), 0.0f, 360.0f, "deg",
		  "Random rotation limit around X, Y, Z", 0, "placement" },
		{ "align_to_terrain", "Align to terrain", DM_SETTING( alignToTerrain ), 0.0f, 0.0f, "", "Instance Y axis along the terrain normal",
		  0, "placement" },
		{ "ground_fit", "Ground fit", DM_SETTING( groundFit ), 0.0f, 0.0f, "",
		  "The instance goes down to the lowest ground under its base footprint (as Embed in PCG): a wide base (stump, log) "
		  "does not hang over a dip", 0, "placement" },
		{ "ground_embed", "Ground embed", DM_SETTING( groundEmbed ), 0.0f, 0.5f, "m",
		  "With Ground fit: deeper into the ground by this times the instance size", settingClamp, "placement" },
		// Флаговые деревья: наклон и рост по открытости ветру (mask_wind_exposure — рельефная часть поля ветра без леса)
		{ "ridge_lean", "Ridge lean", DM_SETTING( ridgeLean ), 0.0f, 30.0f, "deg",
		  "Flagged trees on open ridges: where the wind scours (mask_wind_exposure), the instance leans downwind of the level "
		  "wind by up to this angle; 0 - upright. Needs the Wind row of the level", settingClamp, "placement" },
		{ "ridge_shrink", "Ridge shrink", DM_SETTING( ridgeShrink ), 0.0f, 0.8f, "",
		  "Flagged trees on open ridges: the instance is smaller by up to this share where the wind scours", settingClamp, "placement" },
		// Полог: лес кладёт кроны в карту, под кронами другие слои реже
		{ "canopy", "Canopy", DM_SETTING( canopy ), 0.0f, 2.0f, "",
		  "Persistent layer (forest): its crowns cover the ground in the canopy map - the crown radius of the LOD0 model times this; "
		  "0 - no canopy. Other layers thin out under it (Under canopy)", settingClamp, "placement" },
		{ "under_canopy", "Under canopy", DM_SETTING( underCanopy ), 0.0f, 1.0f, "",
		  "Density under a full canopy (crowns of the forest and of tall level models): 1 - as in the open, 0 - nothing under "
		  "the crowns", settingClamp, "placement" },
		{ "under_canopy_patches", "Under canopy patches", DM_SETTING( underCanopyPatches ), 0.0f, 1.0f, "",
		  "Under the crowns the layer grows in patches (light gaps), not as an even sparse grid: 0 - even, 1 - patches and bare "
		  "ground between them; the mean density stays about Under canopy", settingClamp, "placement" },
		{ "under_canopy_patch_size", "Under canopy patch size", DM_SETTING( underCanopyPatchSize ), 0.25f, 10.0f, "m",
		  "Size of the patches under the crowns (Under canopy patches)", settingClamp, "placement" },
		{ "cast_shadow", "Cast shadow", DM_SETTING( castShadow ), 0.0f, 0.0f, "", "", 0, "placement" },
		// Постоянный слой (лес) — раскладка раз на всю карту при загрузке
		{ "persistent", nullptr, DM_SETTING( persistent ) },
		// Импостер — последний LOD варианта дальше impostor_distance (верхние границы дальностей ставит слой по кольцу)
		{ "impostor_distance", "Impostor distance", DM_SETTING( impostorDistance ), 0.1f, 1.0f, "m",
		  "From this distance an instance is an impostor (the last LOD of the model - up to it); model LODs that started beyond it "
		  "at loading are not used", 0, "impostor" },
		{ "shadow_impostor_distance", "Shadow impostor distance", DM_SETTING( shadowImpostorDistance ), 0.0f, 1.0f, "m",
		  "Shadow views use the impostor from this distance (0 - as in the frame)", 0, "impostor" },
		{ "impostor_cast_shadow", "Impostor shadow", DM_SETTING( impostorCastShadow ), 0.0f, 0.0f, "",
		  "If impostors cast a shadow: off - shadow views skip them (far grass)", 0, "impostor" },
		// Запекание импостера: правка — кадры заново, когда ползунок отпущен (Scatterer::rebakeChangedImpostors)
		{ "impostor_frame_size", "Impostor frame size", DM_SETTING( impostorFrameSize ), 0.0f, 0.0f, "texels",
		  "Side of an impostor frame (8 x 8 frames): smaller - less memory and a faster bake (64 - a quarter of 128); the impostor is "
		  "rebaked", 0, "impostor", { 16, 32, 64, 128, 256, 512 } },
		{ "impostor_density", "Impostor density", DM_SETTING( impostorDensity ), 0.25f, 4.0f, "",
		  "Coverage multiplier of the bake: > 1 - denser (thin blades and needles far away merge instead of being cut by the "
		  "alpha test); rebaked when the edit ends", 0, "impostor" },
		{ "impostor_occlusion", "Impostor occlusion", DM_SETTING( impostorOcclusion ), 0.0f, 1.0f, "",
		  "Sky light occlusion inside the crown, baked from the frames depth; rebaked when the edit ends", settingClamp, "impostor" },
		{ "impostor_base_shade", "Impostor base shade", DM_SETTING( impostorBaseShade ), 0.0f, 1.0f, "",
		  "Impostor color at the root x (1 - shade), up to the model top - none: far grass without a shadow is not lighter than "
		  "the shadowed grass next to it", settingClamp, "impostor" },
		{ "impostor_self_shadow", "Impostor self shadow", DM_SETTING( impostorSelfShadow ), 0.0f, 1.0f, "",
		  "Sun shadow of the crown on itself from the impostor frames: beyond the shadow distance there are no cascades, and "
		  "without it a far crown is lit from all sides - pale and flat", settingClamp, "impostorSelfShadow" },
	},
	// Разделы окна
	{ { "cell_size", "Distribution" }, { "near_border", "Ring" }, { "size_multiplier", "Distribution" }, { "ground_color", "Color" }, { "edge_spread", "Ring" }, { "layer_filter", "Distribution" }, { "rotation_x,rotation_y,rotation_z", "Placement" }, { "canopy", "Canopy" }, { "cast_shadow", "Shadow" }, { "impostor_distance", "Impostor" } } );
	return schema;
}

const SettingsSchema<WindFieldSettings>& windFieldSchema()
{
	using Settings = WindFieldSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "field_layer_height", "Layer height", DM_SETTING( layerHeight ), 10.0f, 1000.0f, "m",
		  "Air layer above the smoothed ground: where the ground rises above it (ridges, rocks by a gap) the layer is thinner and the wind "
		  "faster; the wind field is rebuilt when the edit ends" },
		{ "field_layer_smoothing", "Layer smoothing", DM_SETTING( layerSmoothing ), 10.0f, 2000.0f, "m",
		  "The top of the air layer follows the ground smoothed this wide; the wind field is rebuilt when the edit ends" },
		{ "field_max_speedup", "Max speedup", DM_SETTING( maxSpeedup ), 1.0f, 5.0f, "",
		  "The wind is at most this many times faster than the wind of the level; the wind field is rebuilt when the edit ends" },
		{ "field_reach", "Lee reach", DM_SETTING( reach ), 10.0f, 1000.0f, "m",
		  "The horizon against the wind is searched this far (lee shelter); the wind field is rebuilt when the edit ends" },
		{ "field_shelter_from", "Lee shelter from", DM_SETTING( shelterFrom ), -30.0f, 30.0f, "deg",
		  "Horizon angle against the wind where the lee shelter starts; the wind field is rebuilt when the edit ends" },
		{ "field_shelter_to", "Lee shelter to", DM_SETTING( shelterTo ), -30.0f, 30.0f, "deg",
		  "Horizon angle against the wind of the full lee shelter; the wind field is rebuilt when the edit ends" },
		{ "field_lee_slowdown", "Lee slowdown", DM_SETTING( leeSlowdown ), 0.0f, 1.0f, "",
		  "Share of the wind speed in the full lee shelter; the wind field is rebuilt when the edit ends" },
		{ "field_forest_damping", "Forest damping", DM_SETTING( forestDamping ), 0.0f, 1.0f, "",
		  "The wind in a dense forest is slower by this share; the wind field is rebuilt when the edit ends" },
		{ "field_forest_length", "Forest length", DM_SETTING( forestLength ), 1.0f, 200.0f, "m",
		  "The wind slows down this far into the forest; the wind field is rebuilt when the edit ends" },
		{ "field_recovery_length", "Recovery length", DM_SETTING( recoveryLength ), 1.0f, 500.0f, "m",
		  "Past the forest the wind speeds up again this far over fields and water; the wind field is rebuilt when the edit ends" },
		{ "field_turbulence", "Turbulence", DM_SETTING( turbulence ), 0.0f, 1.0f, "",
		  "Gustiness behind ridges and at forest edges; the wind field is rebuilt when the edit ends" },
	},
	// Разделы окна
	{ { "field_layer_height", "Flow" }, { "field_reach", "Lee" }, { "field_forest_damping", "Forest" }, { "field_turbulence", "Flow" } } );
	return schema;
}

const SettingsSchema<WindSettings>& windSchema()
{
	using Settings = WindSettings;
	static const SettingsSchema<Settings> schema = {
		{ "strength", "Strength", DM_SETTING( strength ), 0.0f, 2.0f },
		{ "speed", "Speed", DM_SETTING( speed ), 0.0f, 20.0f, "m/s" },
		{ "min_gust_amount", "Min gust amount", DM_SETTING( minGustAmount ), 0.0f, 2.0f },
		{ "max_gust_amount", "Max gust amount", DM_SETTING( maxGustAmount ), 0.0f, 2.0f },
		{ "gust_size", "Gust size", DM_SETTING( gustSize ), 1.0f, 200.0f, "m" },
	};
	return schema;
}

const SettingsSchema<HeightFogSettings>& heightFogSchema()
{
	using Settings = HeightFogSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		// Плотность — от тысячных до сотых 1/м: логарифмический ползунок
		{ "density", "Density", DM_SETTING( layer.density ), 0.0f, 0.02f, "1/m",
		  "Haze over the valley: extinction coefficient; visibility in the layer is about 3 / density", settingLogarithmic },
		{ "height", "Height", DM_SETTING( layer.height ), -100.0f, 500.0f, "m", "Below this height the layer is uniform, above it thins out" },
		{ "height_falloff", "Height falloff", DM_SETTING( layer.heightFalloff ), 0.0f, 0.5f, "1/m",
		  "Above the height the density drops e times every 1 / falloff metres" },
		{ "second_density", "Second density", DM_SETTING( secondLayer.density ), 0.0f, 0.2f, "1/m", "Fog poured into the lowlands",
		  settingLogarithmic },
		{ "second_height", "Second height", DM_SETTING( secondLayer.height ), -100.0f, 500.0f, "m" },
		{ "second_height_falloff", "Second height falloff", DM_SETTING( secondLayer.heightFalloff ), 0.0f, 1.0f, "1/m" },
		{ "albedo", "Albedo", DM_SETTING( albedo ), 0.0f, 0.0f, "", "", settingColor },
		{ "scattering_distribution", "Scattering distribution", DM_SETTING( scatteringDistribution ), -0.9f, 0.9f, "",
		  "Henyey-Greenstein g: > 0 - fog glows around the sun" },
		{ "volumetric", "Volumetric", DM_SETTING( volumetric ), 0.0f, 0.0f, "", "Froxel volume with sun shadows and lamps; off - analytic fog only" },
		{ "view_distance", "View distance", DM_SETTING( viewDistance ), 20.0f, 1000.0f, "m",
		  "Depth of the froxel volume; beyond it the fog is analytic, without shadows" },
		{ "sky_distance", "Sky distance", DM_SETTING( skyDistance ), 500.0f, 100000.0f, "m",
		  "Fog over the sky background is taken as over a point this far", settingLogarithmic | settingClampLow },
	},
	// Разделы окна
	{ { "density", "Layer 1" }, { "second_density", "Layer 2" }, { "albedo", "Scattering" }, { "volumetric", "Volume" }, { "sky_distance", "Scattering" } } );
	return schema;
}

const SettingsSchema<SkyAtmosphereSettings>& skyAtmosphereSchema()
{
	using Settings = SkyAtmosphereSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "sky_intensity", "Sky intensity", DM_SETTING( skyIntensity ), 0.0f, 10.0f },
		{ "haze", "Haze", DM_SETTING( haze ), 0.0f, 10.0f },
		{ "ground_albedo", "Ground albedo", DM_SETTING( groundAlbedo ), 0.0f, 1.0f },
		{ "aerial_perspective_view_distance_scale", "Aerial perspective view distance scale", DM_SETTING( aerialPerspectiveViewDistanceScale ),
		  0.0f, 16.0f },
		{ "aerial_perspective_scaled_distance", "Aerial perspective scaled distance", DM_SETTING( aerialPerspectiveScaledDistance ),
		  0.0f, 20000.0f, "m", "The air is thickened by the view distance scale only up to this distance from the camera, farther - "
		  "the real path: the valley looks big, the far mountains don't drown in the haze; 0 - the whole path", settingClampLow },
		{ "night_sky_luminance", "Night sky luminance", DM_SETTING( nightSkyLuminance ), 0.0f, 0.01f, "cd/m2", "", settingDrag },
		// Млечный Путь: файл — только в базе (загружается при запуске), в окне — яркость: средняя по сфере (в полосе Галактики
		// — втрое ярче); видно ночью, как звёзды
		{ "milky_way", nullptr, DM_SETTING( milkyWay ) },
		{ "milky_way_luminance", "Milky Way luminance", DM_SETTING( milkyWayLuminance ), 0.0f, 0.001f, "cd/m2",
		  "Mean over the whole sky; the galactic plane is about three times brighter", settingDrag },
	},
	// Разделы окна
	{ { "sky_intensity", "Atmosphere" }, { "aerial_perspective_view_distance_scale", "Aerial perspective" }, { "night_sky_luminance", "Night sky" } } );
	return schema;
}

const SettingsSchema<HDRIBackdropSettings>& hdriBackdropSchema()
{
	using Settings = HDRIBackdropSettings;
	static const SettingsSchema<Settings> schema = {
		{ "texture", nullptr, DM_SETTING( texture ) },
		{ "intensity", "Intensity", DM_SETTING( intensity ), 0.0f, 0.0f, "cd/m2", "", settingDrag },
		{ "rotation", "Rotation", DM_SETTING( rotation ), -180.0f, 180.0f, "deg" },
		{ "max_luminance", "Max luminance", DM_SETTING( maxLuminance ), 0.0f, 0.0f, "", "", settingDrag },
	};
	return schema;
}

namespace
{

// Поля части цветокоррекции (ColorGradingPart) — подокно part окна «Color grading»: колонки как свойства Color Grading в UE,
// color_saturation + suffix (_shadows, _midtones, _highlights; у всего кадра — без него)
template<ColorGradingPart ColorGradingSettings::* part>
void addColorGradingPart( SettingsSchema<PostProcessSettings>& schema, const std::string& suffix, const std::string& group )
{
	using Settings = PostProcessSettings;
	const std::string channels = " R, G, B and the overall multiplier (W)";
	schema.push_back( { "color_saturation" + suffix, "Saturation",
						[]( Settings& s ) -> XMFLOAT4& { return ( s.colorGrading.*part ).saturation; }, 0.0f, 2.0f, "",
						"Saturation:" + channels + "; 0 - grey of the same luminance, 1 - as is", 0, group } );
	schema.push_back( { "color_contrast" + suffix, "Contrast",
						[]( Settings& s ) -> XMFLOAT4& { return ( s.colorGrading.*part ).contrast; }, 0.0f, 2.0f, "",
						"Contrast around the middle grey (0.18):" + channels + "; 1 - as is", 0, group } );
	schema.push_back( { "color_gamma" + suffix, "Gamma",
						[]( Settings& s ) -> XMFLOAT4& { return ( s.colorGrading.*part ).gamma; }, 0.0f, 2.0f, "",
						"Gamma:" + channels + "; more - brighter midtones, 1 - as is", 0, group } );
	schema.push_back( { "color_gain" + suffix, "Gain",
						[]( Settings& s ) -> XMFLOAT4& { return ( s.colorGrading.*part ).gain; }, 0.0f, 2.0f, "",
						"Multiplier:" + channels + "; 1 - as is", 0, group } );
	schema.push_back( { "color_offset" + suffix, "Offset",
						[]( Settings& s ) -> XMFLOAT4& { return ( s.colorGrading.*part ).offset; }, -0.5f, 0.5f, "",
						"Added to the color after the gain (the middle grey is 0.18):" + channels + " added together; 0 - as is",
						0, group } );
}

}

const SettingsSchema<PostProcessSettings>& postProcessSchema()
{
	using Settings = PostProcessSettings;
	static const SettingsSchema<Settings> schema = [] {
	SettingsSchema<Settings> schema = {
		{ "manual_ev100", "Manual EV100", DM_SETTING( manualEV100 ), -10.0f, 20.0f, "", "", 0, "exposure" },
		{ "exposure_compensation", "Compensation", DM_SETTING( exposureCompensation ), -6.0f, 6.0f, "EV", "", 0, "exposure" },
		{ "min_ev100", "Min EV100", DM_SETTING( minEV100 ), -10.0f, 20.0f, "", "", 0, "exposureAdaptation" },
		{ "max_ev100", "Max EV100", DM_SETTING( maxEV100 ), -10.0f, 20.0f, "", "", 0, "exposureAdaptation" },
		{ "histogram_low_percent", "Histogram low", DM_SETTING( histogramLowPercent ), 0.0f, 100.0f, "%", "", 0, "exposureAdaptation" },
		{ "histogram_high_percent", "Histogram high", DM_SETTING( histogramHighPercent ), 0.0f, 100.0f, "%", "", 0, "exposureAdaptation" },
		{ "speed_up", "Speed up", DM_SETTING( speedUp ), 0.0f, 20.0f, "1/s", "", 0, "exposureAdaptation" },
		{ "speed_down", "Speed down", DM_SETTING( speedDown ), 0.0f, 20.0f, "1/s", "", 0, "exposureAdaptation" },
		{ "bloom_intensity", "Intensity", DM_SETTING( bloomIntensity ), 0.0f, 8.0f, "", "", 0, "bloom" },
		{ "bloom_threshold", "Threshold", DM_SETTING( bloomThreshold ), -1.0f, 10.0f, "", "", 0, "bloom" },
		{ "purkinje_shift", "Purkinje shift", DM_SETTING( purkinjeShift ), 0.0f, 1.0f, "",
		  "Night vision: in the dark the color goes into a bluish monochrome (Krawczyk 2005); 0 - off", 0, "nightVision" },
		// Лучи-звёзды вокруг ярких бликов (PostProcess::renderStars)
		{ "star_intensity", "Intensity", DM_SETTING( starIntensity ), 0.0f, 1.0f, "",
		  "Rays around bright highlights (sun sparkles on water, wet stones, metal): diffraction on the aperture blades; "
		  "share of the light above the threshold spread into the rays; 0 - off", 0, "stars" },
		{ "star_opacity", "Opacity", DM_SETTING( starOpacity ), 0.0f, 1.0f, "",
		  "Visibility of the stars: their size and shape stay as they are, the stars are blended over the frame after the "
		  "tonemapping (Star intensity changes the brightness, and with it how long the rays look)", 0, "stars" },
		{ "star_threshold", "Threshold", DM_SETTING( starThreshold ), 0.0f, 2000.0f, "",
		  "Brightness after exposure from which a highlight casts rays (1 - white; sun sparkles on water - hundreds)", settingLogarithmic,
		  "stars" },
		{ "star_limit", "Limit", DM_SETTING( starLimit ), 1.0f, 10000.0f, "",
		  "Soft limit of the brightness above the threshold that goes into the rays: the sun is thousands of times brighter "
		  "than a sparkle, and without it its rays would reach the end of the kernel", settingLogarithmic, "stars" },
		{ "star_rays", "Rays", DM_SETTING( starRays ), 0.0f, 0.0f, "", "Number of rays of the star", 0, "stars", { 4, 6, 8 } },
		{ "star_angle", "Angle", DM_SETTING( starAngle ), 0.0f, 180.0f, "deg", "Rotation of the star", 0, "stars" },
		{ "star_length", "Length", DM_SETTING( starLength ), 0.01f, 0.2f, "", "Ray length, share of the frame height", 0, "stars" },
		{ "star_dispersion", "Dispersion", DM_SETTING( starDispersion ), 0.0f, 4.0f, "",
		  "Rainbow fringe of the rays: diffraction grows with the wavelength, the red ray is longer than the blue one "
		  "(1 - by the wavelength ratio, more - stronger); 0 - white rays", 0, "stars" },
		// Лучи солнца — god rays (PostProcess::renderLightShafts)
		{ "light_shaft_intensity", "Intensity", DM_SETTING( lightShaftIntensity ), 0.0f, 4.0f, "",
		  "God rays from the sun (screen space, like Light Shafts in UE): bright sky near the sun blurred toward it, silhouettes "
		  "of trees and mountains cut it into rays; not physical in clear dry air, but pretty; 0 - off", 0, "lightShafts" },
		{ "light_shaft_threshold", "Threshold", DM_SETTING( lightShaftThreshold ), 0.0f, 100.0f, "",
		  "Sky brightness after exposure from which it shines into the rays (1 - white)", settingLogarithmic, "lightShafts" },
		{ "light_shaft_limit", "Limit", DM_SETTING( lightShaftLimit ), 1.0f, 1000.0f, "",
		  "Soft limit of the brightness above the threshold: the sun disk is thousands of times brighter than the sky near it",
		  settingLogarithmic, "lightShafts" },
		{ "light_shaft_radius", "Radius", DM_SETTING( lightShaftRadius ), 0.05f, 2.0f, "",
		  "How far from the sun the sky shines into the rays, share of the frame height", 0, "lightShafts" },
		{ "light_shaft_length", "Length", DM_SETTING( lightShaftLength ), 0.0f, 1.0f, "",
		  "Share of the way to the sun a pixel gathers the light from: longer rays", 0, "lightShafts" },
		{ "light_shaft_tint", "Tint", DM_SETTING( lightShaftTint ), 0.0f, 0.0f, "", "", settingColor, "lightShafts" },
		{ "light_shaft_distance", "Distance", DM_SETTING( lightShaftDistance ), 0.0f, 500.0f, "m",
		  "The rays are light scattered in the air in front of what is behind the pixel: share 1 - e^(-depth / distance); no haze "
		  "over near branches and trunks; 0 - the same at any depth", settingLogarithmic, "lightShafts" },
		{ "light_shaft_decay", "Decay", DM_SETTING( lightShaftDecay ), 0.001f, 1.0f, "",
		  "Weight of the light at the end of the trail toward the sun (Decay in GPU Gems 3, ch. 13): less - shorter and sharper "
		  "rays; 1 - no falloff", settingLogarithmic, "lightShafts" },
		{ "light_shaft_fade_radius", "Fade radius", DM_SETTING( lightShaftFadeRadius ), 0.0f, 2.0f, "",
		  "The rays fade out in a circle around the sun: radius, share of the frame height; 0 - they reach the frame edge", 0,
		  "lightShafts" },
		{ "light_shaft_fade_width", "Fade width", DM_SETTING( lightShaftFadeWidth ), 0.01f, 1.0f, "",
		  "Width of the fade band, share of the fade radius: 1 - fading from the sun itself", 0, "lightShafts" },
		// Экранное затенение окружением (AmbientOcclusion) — до проходов цвета, но настройки здесь, как в UE
		{ "ambient_occlusion_intensity", "Intensity", DM_SETTING( ambientOcclusion.intensity ), 0.0f, 1.0f, "",
		  "Screen space ambient occlusion (GTAO): how much it darkens the sky light; 0 - off", 0, "ambientOcclusion" },
		{ "ambient_occlusion_radius", "Radius", DM_SETTING( ambientOcclusion.radius ), 0.1f, 5.0f, "m", "", 0, "ambientOcclusion" },
		{ "ambient_occlusion_power", "Power", DM_SETTING( ambientOcclusion.power ), 0.5f, 4.0f, "",
		  "Visibility^power: stronger in crevices, weaker in the open", 0, "ambientOcclusion" },
		// Цветокоррекция (ColorGradingLut) — подокно «Color grading»: баланс белого, части кадра — вложенные подокна
		{ "white_temp", "Temperature", DM_SETTING( colorGrading.whiteTemp ), 1500.0f, 15000.0f, "K",
		  "White balance, like a camera: color temperature of the light that looks white; higher - warmer picture, lower - "
		  "cooler; 6500 - as is", settingClamp, "colorGrading" },
		{ "white_tint", "Tint", DM_SETTING( colorGrading.whiteTint ), -1.0f, 1.0f, "",
		  "White balance across the temperature: more - magenta, less - green; 0 - as is", settingClamp, "colorGrading" },
	};
	addColorGradingPart<&ColorGradingSettings::global>( schema, "", "colorGradingGlobal" );
	addColorGradingPart<&ColorGradingSettings::shadows>( schema, "_shadows", "colorGradingShadows" );
	schema.push_back( { "color_correction_shadows_max", "Shadows max", DM_SETTING( colorGrading.shadowsMax ), 0.001f, 1.0f, "",
						"Luminance after the exposure (the middle grey is 0.18) where the shadows end: their share is "
						"1 - smoothstep( 0, max, luminance )", settingClampLow, "colorGradingShadows" } );
	addColorGradingPart<&ColorGradingSettings::midtones>( schema, "_midtones", "colorGradingMidtones" );
	addColorGradingPart<&ColorGradingSettings::highlights>( schema, "_highlights", "colorGradingHighlights" );
	schema.push_back( { "color_correction_highlights_min", "Highlights min", DM_SETTING( colorGrading.highlightsMin ), 0.0f, 4.0f,
						"", "Luminance after the exposure where the highlights start: their share is "
						"smoothstep( min, max, luminance ); the midtones get the rest", settingClampLow, "colorGradingHighlights" } );
	schema.push_back( { "color_correction_highlights_max", "Highlights max", DM_SETTING( colorGrading.highlightsMax ), 0.0f, 8.0f,
						"", "Luminance after the exposure where the colors are fully highlights", settingClampLow,
						"colorGradingHighlights" } );
	return schema;
	}();
	return schema;
}

const SettingsSchema<SunPositionSettings>& sunPositionSchema()
{
	using Settings = SunPositionSettings;
	static const SettingsSchema<Settings> schema = {
		{ "latitude", "Latitude", DM_SETTING( latitude ), -90.0f, 90.0f, "deg" },
		{ "longitude", "Longitude", DM_SETTING( longitude ), -180.0f, 180.0f, "deg" },
		{ "time_zone", "Time zone", DM_SETTING( timeZone ), -12.0f, 14.0f, "h" },
		{ "north_offset", "North offset", DM_SETTING( northOffset ), -180.0f, 180.0f, "deg" },
		// Месяц и день — в окне; в базе — дата текстом (колонка date, у SunPosition вместе с годом)
		{ "", "Month", DM_SETTING( month ), 1.0f, 12.0f },
		{ "", "Day", DM_SETTING( day ), 1.0f, 31.0f },
		{ "time_of_day", "Time of day", DM_SETTING( timeOfDay ), 0.0f, 24.0f, "h" },
		{ "time_scale", "Time scale", DM_SETTING( timeScale ), 0.0f, 100000.0f, "", "", settingDrag },
	};
	return schema;
}

const SettingsSchema<TerrainErosionSettings>& terrainErosionSchema()
{
	using Settings = TerrainErosionSettings;
	// Эрозия считается при загрузке уровня — к подсказке
	const std::string load = "; applied at the next level load";
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "droplets", "Droplets", DM_SETTING( droplets ), 0.0f, 4000000.0f, "", "Water droplets of the hydraulic erosion" + load, settingClampLow },
		{ "seed", "Seed", DM_SETTING( seed ), 0.0f, 1000.0f, "", "Random numbers of the droplets" + load },
		{ "lifetime", "Lifetime", DM_SETTING( lifetime ), 1.0f, 300.0f, "steps", "Steps of a droplet, a texel each" + load, settingClampLow },
		{ "inertia", "Inertia", DM_SETTING( inertia ), 0.0f, 1.0f, "", "Share of the previous direction" + load },
		{ "capacity", "Capacity", DM_SETTING( capacity ), 0.0f, 8.0f, "", "Sediment capacity: slope * speed * water * capacity" + load },
		{ "min_slope", "Min slope", DM_SETTING( minSlope ), 0.0f, 0.1f, "", "Slope in the capacity at least" + load },
		{ "erode_speed", "Erode speed", DM_SETTING( erodeSpeed ), 0.0f, 1.0f, "", "Share of the missing capacity eroded per step" + load },
		{ "deposit_speed", "Deposit speed", DM_SETTING( depositSpeed ), 0.0f, 1.0f, "", "Share of the excess sediment deposited per step" + load },
		{ "evaporation", "Evaporation", DM_SETTING( evaporation ), 0.0f, 0.1f, "", "Water evaporated per step" + load },
		{ "gravity", "Gravity", DM_SETTING( gravity ), 0.0f, 20.0f, "", "Speed-up downhill" + load },
		{ "radius", "Radius", DM_SETTING( radius ), 1.0f, 12.0f, "texels", "Erosion brush" + load, settingClampLow },
		{ "rain_scale", "Rain scale", DM_SETTING( rainScale ), 10.0f, 2000.0f, "m", "Size of the rain patches" + load },
		{ "rain_min", "Rain min", DM_SETTING( rainMin ), 0.0f, 1.0f, "", "Rain share in dry places (1 - even rain)" + load },
		{ "talus_angle", "Talus angle", DM_SETTING( talusAngle ), 10.0f, 80.0f, "deg", "Angle of repose of the scree" + load },
		{ "thermal_iterations", "Thermal iterations", DM_SETTING( thermalIterations ), 0.0f, 200.0f, "", "Steps of the scree sliding" + load,
		  settingClampLow },
		{ "thermal_rate", "Thermal rate", DM_SETTING( thermalRate ), 0.0f, 1.0f, "", "Share of the excess sliding per step" + load },
	},
	// Разделы окна
	{ { "droplets", "Droplets" }, { "rain_scale", "Rain" }, { "talus_angle", "Talus" } } );
	return schema;
}

const SettingsSchema<TerrainSettings>& terrainSchema()
{
	using Settings = TerrainSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "height_multiplier", "Height multiplier", DM_SETTING( heightMultiplier ), 1.0f, 5000.0f, "m",
		  "Height of the heightmap value 1; the scatter and particles follow, the terrain edits do not" },
		{ "triplanar_sharpness", "Triplanar sharpness", DM_SETTING( triplanarSharpness ), 1.0f, 32.0f, "",
		  "How sharply steep slopes switch from the top projection to the side ones" },
		{ "height_blend", "Height blend", DM_SETTING( heightBlend ), 0.01f, 1.0f, "", "Depth of the height-based blend between layers" },
		// Второй масштаб текстур вдали (distance resampling, как в материалах UE Landscape): мелкий повтор слоёв издалека
		// складывается в сетку
		{ "far_texture_scale", "Far texture scale", DM_SETTING( farTextureScale ), 1.0f, 32.0f, "",
		  "Texture repeat multiplier far away: the small repeat does not fold into a grid" },
		// Полоса перехода ко второму масштабу — начало и конец одним двойным полем. Обе выборки — на всём её протяжении, поэтому
		// цена второго масштаба растёт с её шириной
		{ "far_blend_start,far_blend_end", "Far blend", DM_SETTING( farBlend ), 0.0f, 1000.0f, "m",
		  "Distance band where the far texture scale replaces the near one: both scales are sampled along it", settingRangeControl },
		// Микротени прямого света по AO слоёв (lighting.sh): в щели между камнями солнце светит только под крутым углом
		{ "micro_shadows", "Micro shadows", DM_SETTING( microShadows ), 0.0f, 1.0f, "",
		  "Direct light in the crevices of the layer AO: it reaches their bottom only from steep angles (micro shadowing of "
		  "Uncharted 4); 0 - AO darkens the sky light only" },
		{ "wet_darkening", "Wet darkening", DM_SETTING( wetDarkening ), 0.05f, 1.0f, "",
		  "Wet ground is darker: the albedo at full wetness is multiplied by this (water in the pores: wet sand and soil are two to "
		  "three times darker than dry ones); 1 - only the gloss", settingClamp },
		// Подстилка под кронами: вес трав уходит слою с ролью forest_floor по карте полога (terrain_layers.sh, applyForestFloor)
		{ "forest_floor", "Forest floor", DM_SETTING( forestFloor ), 0.0f, 1.0f, "",
		  "Forest floor under the tree crowns: share of the canopy cover that moves the grass weight to the forest_floor layer "
		  "(0 - grass under the trees as in the open)" },
		{ "forest_floor_edge", "Forest floor edge", DM_SETTING( forestFloorEdge ), 0.0f, 4.0f, "m",
		  "How far the noise moves the edge of the forest floor from the crown circle: a ragged edge instead of a disc" },
		{ "forest_moss", "Forest moss", DM_SETTING( forestMoss ), 0.0f, 0.8f, "",
		  "Share of the forest floor that goes to the forest_moss layer where the ground is wet (where wet grass would grow); "
		  "the moss shows in patches by its layer height (above 0.8 the gaps between the patches lose the floor)", settingClamp },
		{ "forest_moss_dry", "Forest moss on dry", DM_SETTING( forestMossDry ), 0.0f, 1.0f, "",
		  "Moss on dry ground: this share of Forest moss (1 - as much as on wet ground)" },
		// Дисплейсмент у камеры (сетка — «Finest mesh»): полный ближе начала полосы, к концу сходит на нет
		{ "displacement_start,displacement_end", "Displacement distance", DM_SETTING( displacement ), 0.0f, 64.0f, "m",
		  "Displacement by the layer heights: full nearer than the start, fades out to the end (end 0 - off); the terrain mesh "
		  "near the camera - Finest mesh", settingRangeControl },
		// Самый мелкий квад сетки у камеры: мельче — камешки выдавливаются ровнее, дороже вершины. Номера — уровни мельче
		// детальной плитки (CDLODTerrain::detailLevels − tileLevels + 1 значений)
		{ "finest_mesh", "Finest mesh", DM_SETTING( finestMesh ), 0.0f, 0.0f, "",
		  "Finest quad of the terrain mesh near the camera; each level halves the quad and the distance (at LOD distance 128 m: "
		  "12.5 cm to 16 m, 6.25 cm to 8 m, 3.1 cm to 4 m, 1.6 cm to 2 m). Finer - the displacement shapes small stones, the vertex "
		  "cost grows by about the same per level", 0, "", { 0, 1, 2, 3, 4 }, { "25 cm", "12.5 cm", "6.25 cm", "3.1 cm", "1.6 cm" } },
		// Где самая мелкая сетка полная. При ходьбе — вокруг ног: LOD от отрезка «глаза — ноги»
		{ "finest_mesh_radius", "Finest mesh radius", DM_SETTING( finestMeshRadius ), 0.0f, 8.0f, "m",
		  "The finest mesh is full nearer, farther it morphs into the next level (up to 1.5 times the radius): around the feet "
		  "when walking, around the camera in flight. Not less than the LOD distance gives" },
	},
	// Разделы окна
	{ { "height_multiplier", "Geometry" }, { "triplanar_sharpness", "Material" }, { "forest_floor", "Forest floor" }, { "displacement_start,displacement_end", "Geometry" } } );
	return schema;
}

const SettingsSchema<FarTerrainSettings>& farTerrainSchema()
{
	using Settings = FarTerrainSettings;
	static const char* rules = "Rules";
	static const char* glaciers = "Glaciers";
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		// Данные генератора (Tools/gen_far_terrain.py, Tools/far_terrain.py) — только в базе
		{ "heightmap", nullptr, DM_SETTING( heightMap ) },
		{ "origin", nullptr, DM_SETTING( origin ) },
		{ "extent", nullptr, DM_SETTING( extent ) },
		{ "height_multiplier", nullptr, DM_SETTING( heightMultiplier ) },
		{ "height_offset", nullptr, DM_SETTING( heightOffset ) },
		{ "lod_distance", "LOD distance", DM_SETTING( lodDistance ), 2000.0f, 64000.0f, "m",
		  "Range of the level whose quad is a texel of the mountain map (32 m); each coarser level reaches twice as far. "
		  "Nearer levels share the ranges of the level map", settingLogarithmic | settingClampLow },
		{ "blend_distance", "Blend distance", DM_SETTING( blendDistance ), 32.0f, 4000.0f, "m",
		  "From the edge of the level map its heights turn into the mountain heights", settingClampLow },
		{ "material_blend", "Material blend", DM_SETTING( materialBlend ), 0.0f, 4000.0f, "m",
		  "From the edge of the level map its painted layers turn into the rules below" },
		{ "macro_distance_from,macro_distance_to", "Macro distance", DM_SETTING( macroDistance ), 0.0f, 32000.0f, "m",
		  "Farther the layer textures repeat at the macro scale: the far repeat does not show as a grid of bumps on distant "
		  "slopes", settingRangeControl },
		{ "macro_texture_scale", "Macro texture scale", DM_SETTING( macroTextureScale ), 1.0f, 64.0f, "",
		  "The macro repeat is this many times larger than the far one (Far texture scale of the terrain)", settingClampLow },
		// Тень солнца на всём кольце: тексель — сторона кольца / разрешение (512 — 128 м); пересчёт — при сдвиге солнца
		{ "shadow_resolution", "Shadow resolution", DM_SETTING( shadowResolution ), 0.0f, 0.0f, "",
		  "Texels of the sun shadow over the whole ring per side: the mountains shade themselves by it; finer - sharper and "
		  "longer to recompute when the sun moves", 0, "", { 256, 512, 1024, 2048 } },
		{ "rock_slope_from,rock_slope_to", "Rock slope", DM_SETTING( rockSlope ), 0.0f, 90.0f, "deg", "Bare rock on slopes steeper",
		  settingRangeControl, rules },
		{ "scree_slope_from,scree_slope_to", "Scree slope", DM_SETTING( screeSlope ), 0.0f, 90.0f, "deg",
		  "Scree on slopes steeper (below the rock)...", settingRangeControl, rules },
		{ "scree_height_from,scree_height_to", "Scree height", DM_SETTING( screeHeight ), 0.0f, 4000.0f, "m", "...higher than this",
		  settingRangeControl, rules },
		{ "meadow_height_from,meadow_height_to", "Meadow height", DM_SETTING( meadowHeight ), 0.0f, 4000.0f, "m",
		  "Valley grass turns into the dry meadow", settingRangeControl, rules },
		{ "snow_height_from,snow_height_to", "Snow height", DM_SETTING( snowHeight ), 0.0f, 5000.0f, "m",
		  "Snow line on slopes facing south", settingRangeControl, rules },
		{ "snow_north", "Snow north", DM_SETTING( snowNorth ), 0.0f, 1500.0f, "m", "On slopes facing north the snow is lower by this",
		  0, rules },
		{ "snow_noise", "Snow noise", DM_SETTING( snowNoise ), 0.0f, 1000.0f, "m", "The snow line wanders in patches by this", 0, rules },
		{ "snow_slope_from,snow_slope_to", "Snow slope", DM_SETTING( snowSlope ), 0.0f, 90.0f, "deg", "Snow does not hold on steeper slopes",
		  settingRangeControl, rules },
		{ "forest_line_from,forest_line_to", "Forest line", DM_SETTING( forestLine ), 0.0f, 4000.0f, "m",
		  "Forest tint of the ground below; it fades out to the end", settingRangeControl, rules },
		{ "forest_slope_from,forest_slope_to", "Forest slope", DM_SETTING( forestSlope ), 0.0f, 90.0f, "deg", "No forest on steeper slopes",
		  settingRangeControl, rules },
		{ "forest_color", "Forest color", DM_SETTING( forestColor ), 0.0f, 0.0f, "", "Forest seen from far away, linear", settingColor,
		  rules },
		{ "glacier_cover_from,glacier_cover_to", "Glacier cover", DM_SETTING( glacierCover ), 0.0f, 200.0f, "m",
		  "Present-day glaciers (Glacial erosion, Present ELA): ice thicker than this is snow on any slope; below the firn line "
		  "(the present ELA) and where snow does not hold it is bare ice", settingRangeControl, glaciers },
		{ "glacier_ice_tint", "Glacier ice tint", DM_SETTING( glacierIceTint ), 0.0f, 0.0f, "",
		  "Bare glacier ice: the snow layer albedo times this", settingColor, glaciers },
		{ "crevasse_slope_from,crevasse_slope_to", "Crevasse slope", DM_SETTING( crevasseSlope ), 0.0f, 90.0f, "deg",
		  "Crevasses across the ice flow where the glacier surface is steeper (icefalls)", settingRangeControl, glaciers },
		{ "crevasse_spacing", "Crevasse spacing", DM_SETTING( crevasseSpacing ), 5.0f, 500.0f, "m",
		  "Crevasse spacing along the flow; far away they turn into their average tone", settingClampLow, glaciers },
		{ "crevasse_color", "Crevasse color", DM_SETTING( crevasseColor ), 0.0f, 0.0f, "", "Albedo inside a crevasse, linear",
		  settingColor, glaciers },
		{ "moraine_width", "Moraine width", DM_SETTING( moraineWidth ), 32.0f, 500.0f, "m",
		  "Debris from the rock walls above a glacier rides its flow lines: lateral moraines along the edges, medial ones from "
		  "where two ice streams join; the moraine map is rebuilt when the edit ends", settingClampLow, glaciers },
		{ "moraine_cover_from,moraine_cover_to", "Moraine cover", DM_SETTING( moraineCover ), 0.0f, 1.0f, "",
		  "Share of the debris lines around a point: the moraine (scree layer) grows from the start to the end; below the firn "
		  "line only", settingRangeControl, glaciers },
	},
	// Разделы окна
	{ { "lod_distance", "Geometry" }, { "material_blend", "Material" }, { "shadow_resolution", "Geometry" }, { "rock_slope_from,rock_slope_to", "Rock and scree" }, { "meadow_height_from,meadow_height_to", "Meadow" }, { "snow_height_from,snow_height_to", "Snow" }, { "forest_line_from,forest_line_to", "Forest" }, { "glacier_cover_from,glacier_cover_to", "Glaciers" } } );
	return schema;
}

const SettingsSchema<FluvialErosionSettings>& fluvialErosionSchema()
{
	using Settings = FluvialErosionSettings;
	static const char* rebuilt = "; the mountains are eroded again when the edit ends";
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "resolution", "Resolution", DM_SETTING( resolution ), 0.0f, 0.0f, "",
		  std::string( "Cells of the river grid per side: finer - narrower valleys, 4 times longer" ) + rebuilt, 0, "", { 512, 1024, 2048 } },
		{ "erodibility", "Erodibility", DM_SETTING( erodibility ), 0.0f, 1.0e-3f, "1/yr",
		  std::string( "K of the stream power law: incision = K * catchment^m * slope" ) + rebuilt, settingLogarithmic },
		{ "area_exponent", "Area exponent", DM_SETTING( areaExponent ), 0.1f, 1.0f, "",
		  std::string( "m of the stream power law: larger - big rivers cut deeper against small streams" ) + rebuilt },
		{ "years", "Years", DM_SETTING( years ), 0.0f, 1.0e6f, "yr", std::string( "Years of the river erosion" ) + rebuilt,
		  settingLogarithmic },
		{ "steps", "Steps", DM_SETTING( steps ), 1.0f, 200.0f, "", std::string( "Steps of the run: the scheme is implicit, more - more exact" ) + rebuilt },
		{ "hillslope_diffusion", "Hillslope diffusion", DM_SETTING( hillslopeDiffusion ), 0.0f, 1.0f, "m2/yr",
		  std::string( "Slopes between the valleys are rounded" ) + rebuilt },
		{ "routing_noise", "Routing noise", DM_SETTING( routingNoise ), 0.0f, 50.0f, "m",
		  std::string( "Noise of the flow direction only (not of the heights): without it on even slopes the streams run in "
					   "parallel furrows along the grid" ) + rebuilt },
		{ "uplift_scale", "Uplift scale", DM_SETTING( upliftScale ), 0.0f, 20000.0f, "m",
		  std::string( "Relief regrown by uplift and erosion (Cordonnier et al. 2016): the envelope of the mountains is the map "
					   "smoothed this wide, the ridges and valleys come from the rivers; 0 - the rivers cut the given relief" ) + rebuilt },
		{ "uplift_time", "Uplift time", DM_SETTING( upliftTime ), 1000.0f, 1.0e7f, "yr",
		  std::string( "Uplift rate - the envelope height per this time" ) + rebuilt, settingLogarithmic },
		{ "uplift_floor", "Uplift floor", DM_SETTING( upliftFloor ), -500.0f, 2000.0f, "m",
		  std::string( "Lowlands of the map lower than this (smoothed) are not uplifted: the basin, the valleys and the sea stay, the "
					   "mountain rivers flow into them" ) + rebuilt },
		{ "map_fade_from,map_fade_to", "Map fade", DM_SETTING( mapFade ), 0.0f, 10000.0f, "m",
		  std::string( "Near the level map the erosion fades out: full farther than the end from its edge" ) + rebuilt, settingRangeControl },
	},
	// Разделы окна
	{ { "resolution", "Rivers" }, { "uplift_scale", "Uplift" }, { "map_fade_from,map_fade_to", "Rivers" } } );
	return schema;
}

const SettingsSchema<GlacialErosionSettings>& glacialErosionSchema()
{
	using Settings = GlacialErosionSettings;
	static const char* rebuilt = "; the mountains are eroded again when the edit ends";
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "resolution", "Resolution", DM_SETTING( resolution ), 0.0f, 0.0f, "",
		  std::string( "Cells of the ice grid per side: finer - narrower troughs, the run is 8 times longer per step" ) + rebuilt, 0, "",
		  { 256, 512, 1024 } },
		{ "ela", "ELA", DM_SETTING( ela ), 0.0f, 4000.0f, "m",
		  std::string( "Equilibrium line altitude of the ice age: snow accumulates above, ice melts below" ) + rebuilt },
		{ "balance_gradient", "Balance gradient", DM_SETTING( balanceGradient ), 0.0f, 20.0f, "m/yr per km",
		  std::string( "Mass balance per kilometre of height above the equilibrium line" ) + rebuilt },
		{ "max_accumulation", "Max accumulation", DM_SETTING( maxAccumulation ), 0.0f, 10.0f, "m/yr",
		  std::string( "Accumulation does not exceed this" ) + rebuilt },
		{ "flow", "Flow", DM_SETTING( flow ), 0.0f, 10.0f, "", std::string( "Multiplier of the ice deformation (Glen's law, temperate ice)" ) + rebuilt },
		{ "sliding", "Sliding", DM_SETTING( sliding ), 0.0f, 10.0f, "", std::string( "Multiplier of the basal sliding: it erodes" ) + rebuilt },
		{ "ice_years", "Ice years", DM_SETTING( iceYears ), 0.0f, 5000.0f, "yr", std::string( "The ice builds up before the erosion" ) + rebuilt },
		{ "erosion_years", "Erosion years", DM_SETTING( erosionYears ), 0.0f, 5000.0f, "yr",
		  std::string( "Years of the erosion; the erosion is accelerated - a step erodes as thousands of years" ) + rebuilt },
		{ "erosion", "Erosion", DM_SETTING( erosion ), 0.0f, 0.05f, "m/m",
		  std::string( "Bed lowering per metre of the basal sliding" ) + rebuilt, settingLogarithmic },
		{ "max_erosion", "Max erosion", DM_SETTING( maxErosion ), 0.0f, 3000.0f, "m", std::string( "The bed goes down not more than this" ) + rebuilt },
		{ "map_fade_from,map_fade_to", "Map fade", DM_SETTING( mapFade ), 0.0f, 10000.0f, "m",
		  std::string( "Near the level map the erosion fades out: full farther than the end from its edge" ) + rebuilt, settingRangeControl },
		{ "polish", "Polish", DM_SETTING( polish ), 0.0f, 1.0f, "", std::string( "Small relief under the ice is smoothed" ) + rebuilt },
		{ "frost_peaks", "Frost peaks", DM_SETTING( frostPeaks ), 0.0f, 400.0f, "m",
		  std::string( "Frost shattering: sharp bare spikes along the convex ridges high up; the shape of the massifs stays; 0 - none" ) +
		  rebuilt },
		{ "frost_peak_spacing", "Frost peak spacing", DM_SETTING( frostPeakSpacing ), 64.0f, 2000.0f, "m",
		  std::string( "Spacing of the spikes along the ridges" ) + rebuilt },
		{ "frost_peak_height_from,frost_peak_height_to", "Frost peak height", DM_SETTING( frostPeakHeight ), 0.0f, 5000.0f, "m",
		  std::string( "The spikes grow from the start to the end of this height above the sea" ) + rebuilt, settingRangeControl },
		{ "frost_peak_fade_from,frost_peak_fade_to", "Frost peak fade", DM_SETTING( frostPeakFade ), 0.0f, 10000.0f, "m",
		  std::string( "Near the level map the spikes fade out only at the seam: full farther than the end from its edge (they do "
					   "not change the massifs - the mountains above the valley get them too)" ) + rebuilt, settingRangeControl },
		{ "present_ela", "Present ELA", DM_SETTING( presentEla ), 0.0f, 5000.0f, "m",
		  std::string( "Present-day glaciers: the same ice without erosion over the final relief (Ice years) with this equilibrium "
					   "line - cirque and valley glaciers below the peaks; it is the firn line on them too" ) + rebuilt },
	},
	// Разделы окна
	{ { "resolution", "Ice" }, { "erosion_years", "Erosion" }, { "frost_peaks", "Frost peaks" }, { "present_ela", "Present glaciers" } } );
	return schema;
}

const SettingsSchema<TerrainLayerRules>& terrainLayerRulesSchema()
{
	using R = TerrainLayerRules;
	// Правки применяются по концу правки — к подсказке; диапазон — колонки <колонка>_from и <колонка>_to
	static const char* rebuilt = "; the layers and masks are rebuilt when the edit ends";
	auto range = []( const char* column, const char* group, const char* name, R::Range R::* field, float low, float high,
					 const char* unit, const char* tooltip )
	{
		const std::string base = column;
		return SettingField<R>{ base + "_from," + base + "_to", name, field, low, high, unit, tooltip + std::string( rebuilt ), 0, group };
	};
	auto value = []( const char* column, const char* group, const char* name, float R::* field, float low, float high,
					 const char* unit, const char* tooltip )
	{
		return SettingField<R>{ column, name, field, low, high, unit, tooltip + std::string( rebuilt ), 0, group };
	};
	static const char* rock = "Rock and snow";
	static const char* stones = "Stones";
	static const char* grass = "Grass and flowers";
	static const char* forest = "Forest";
	// Полоса у воды (кусты и трава по берегам) и кусты склонов — разными подокнами: «Shrubs» одним списком не говорил, что
	// ширина берегов здесь
	static const char* banks = "Water banks";
	static const char* shrubs = "Slope shrubs";
	static const char* beds = "Water beds";
	static const char* wind = "Wind";
	static const char* shape = "Shape";
	static const SettingsSchema<R> schema = {
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
		value( "bank_width", banks, "Bank width", &R::bankWidth, 0.0f, 10.0f, "texels",
			   "Strip along the streams and lakes for the bank shrubs and grass: this many texels wider than the channel, whole texels" ),
		range( "riparian_density", banks, "Bank shrubs", &R::riparianDensity, 0.0f, 1.0f, "",
			   "Shrubs on the bank strip (mask_shrubs_riparian) in patches by noise 0..1: from, to" ),
		range( "riparian_grass", banks, "Bank grass", &R::riparianGrass, 0.0f, 1.0f, "",
			   "Grass on the bank strip (mask_grass_riparian), thinned by noise 0..1 to a half at most: from, to" ),
		range( "riparian_height", banks, "Bank height", &R::riparianHeight, 0.0f, 1.0f, "",
			   "Bank strip only lower, share of the height multiplier: from, to" ),
		value( "bank_grass_gap", banks, "Bank grass gap", &R::bankGrassGap, 0.0f, 5.0f, "m",
			   "The bank grass grows from 0 at the water to full this far from it (0 - up to the waterline: in the water the scatter clears it)" ),
		value( "bank_shrubs_gap", banks, "Bank shrubs gap", &R::bankShrubsGap, 0.0f, 5.0f, "m",
			   "The bank shrubs grow from 0 at the water to full this far from it" ),
		range( "lake_sediment_height", beds, "Lake sediment height", &R::lakeSedimentHeight, 0.0f, 1.0f, "",
			   "Silt and gravel on the bottom of the lakes lower than this (water level, share of the height multiplier): from, to; "
			   "higher lakes keep pebbles" ),
		range( "lake_gravel_depth", beds, "Lake gravel depth", &R::lakeGravelDepth, 0.0f, 3.0f, "m",
			   "Pebbles of the wave zone give way to gravel (lake_gravel) this deep: from, to" ),
		range( "lake_silt_depth", beds, "Lake silt depth", &R::lakeSiltDepth, 0.0f, 5.0f, "m",
			   "Gravel gives way to silt (lake_silt) this deep: from, to" ),
		range( "stream_gravel_speed", beds, "Stream gravel speed", &R::streamGravelSpeed, 0.0f, 2.0f, "m/s",
			   "Gravel on the bottom of the streams where the current is slower (pools): from, to" ),
		range( "stream_gravel_depth", beds, "Stream gravel depth", &R::streamGravelDepth, 0.0f, 1.0f, "m",
			   "Stream gravel only where the water is deeper (the middle of the bed): from, to" ),
		range( "lake_shallow_depth", beds, "Lake shallows depth", &R::lakeShallowDepth, 0.0f, 3.0f, "m",
			   "Scatter layers with Water = Shallows (cattail) grow in the shore strip of the lakes (from the waterline to the "
			   "overflow, Lake freeboard of Channels) and in the water up to this depth: full density at from, none at to" ),
		value( "lake_shore_width", beds, "Lake shore width", &R::lakeShoreWidth, 0.0f, 20.0f, "m",
			   "The shore strip of the lakes (below the overflow: no meadow grass, cattail of Shallows layers) is at most this wide "
			   "from the water: on a flat valley floor the ground below the overflow runs for tens of metres" ),
		value( "wet_bank_height", beds, "Wet bank height", &R::wetBankHeight, 0.0f, 1.0f, "m",
			   "The ground is wet this high above the water (capillaries, small waves), then dry: the summer sun dries the stones fast" ),
		value( "wet_bank_edge", beds, "Wet bank edge", &R::wetBankEdge, 0.001f, 0.2f, "m",
			   "Height of the wet-to-dry edge above the wet band: small - a sharp line along the stones" ),
		value( "wet_splash_height", beds, "Wet splash height", &R::wetSplashHeight, 0.0f, 2.0f, "m",
			   "By fast water (0.5 to 1.5 m/s of the stream) the wet band is this much higher: splashes" ),
		value( "wet_roughness", beds, "Wet roughness", &R::wetRoughness, 0.0f, 1.0f, "",
			   "Roughness of the wet ground: water in the pores makes it smooth, with a highlight" ),
		range( "krummholz_height", shrubs, "Krummholz height", &R::krummholzHeight, 0.0f, 1.0f, "", "Krummholz above the treeline: from, to" ),
		range( "krummholz_top", shrubs, "Krummholz top", &R::krummholzTop, 0.0f, 1.0f, "", "Krummholz ends higher: from, to" ),
		range( "dry_gaps_slope", shrubs, "Dry gaps slope", &R::dryGapsSlope, 0.0f, 90.0f, "deg", "Shrubs in the gaps of dry slopes: from, to" ),
		value( "dry_gaps_share", shrubs, "Dry gaps share", &R::dryGapsShare, 0.0f, 1.0f, "", "Weight of the shrubs in the gaps of dry slopes" ),
		range( "snow_scour_speed", wind, "Snow scour speed", &R::snowScourSpeed, 0.0f, 3.0f, "",
			   "Snow is blown away where the wind over the terrain (wind field without the forest) is this many times the wind of the level: from, to" ),
		range( "snow_drift_speed", wind, "Snow drift speed", &R::snowDriftSpeed, 0.0f, 3.0f, "",
			   "Snow drifts where the wind over the terrain is this slow (lee of ridges): from, to" ),
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
	return schema;
}

const SettingsSchema<WaterSimulationSettings>& waterSimulationSchema()
{
	using Settings = WaterSimulationSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "time_scale", "Time scale", DM_SETTING( timeScale ), 0.0f, 100.0f },
		{ "source_rate", "Source rate", DM_SETTING( sourceRate ), 0.0f, 2.0f, "l/s" },
		{ "flow_start", "Flow start", DM_SETTING( flowStart ), 100.0f, 100000.0f, "m2" },
		{ "flow_full", "Flow full", DM_SETTING( flowFull ), 100.0f, 1000000.0f, "m2" },
		{ "source_radius", "Source radius", DM_SETTING( sourceRadius ), 0.0f, 16.0f, "m" },
		{ "rain", "Rain", DM_SETTING( rain ), 0.0f, 50.0f, "mm/h" },
		{ "evaporation", "Evaporation", DM_SETTING( evaporation ), 0.0f, 50.0f, "mm/h" },
		{ "manning", "Manning roughness", DM_SETTING( manning ), 0.0f, 0.2f, "s/m^(1/3)" },
		// Шаг и прогрев симуляции — только в базе
		{ "time_step", nullptr, DM_SETTING( timeStep ) },
		{ "warmup_time", nullptr, DM_SETTING( warmupTime ) },
		// Материал поверхности — подокно «Surface»
		{ "absorption", "Absorption", DM_SETTING( absorption ), 0.0f, 5.0f, "1/m", "Absorption per channel R, G, B", settingDrag, "surface" },
		{ "scatter_color", "Scatter color", DM_SETTING( scatterColor ), 0.0f, 0.0f, "", "", settingColor, "surface" },
		{ "scatter_strength", "Scatter strength", DM_SETTING( scatterStrength ), 0.0f, 0.1f, "", "", 0, "surface" },
		{ "roughness", "Roughness", DM_SETTING( roughness ), 0.0f, 0.5f, "", "", 0, "surface" },
		{ "ripple_scale", "Ripple scale", DM_SETTING( rippleScale ), 0.5f, 20.0f, "m", "", 0, "surface" },
		{ "ripple_strength", "Ripple strength", DM_SETTING( rippleStrength ), 0.0f, 4.0f, "", "", 0, "surface" },
		{ "calm_ripple", "Calm ripple", DM_SETTING( calmRipple ), 0.0f, 2.0f, "", "", 0, "surface" },
		{ "refraction", "Refraction", DM_SETTING( refraction ), 0.0f, 0.1f, "1/m", "Refraction offset, share of the screen height per metre of water",
		  0, "surface" },
		{ "ripple_stretch", "Ripple stretch", DM_SETTING( rippleStretch ), 1.0f, 6.0f, "",
		  "How many times the ripple pattern is longer along the flow than across it on running water", 0, "surface" },
		{ "standing_waves", "Standing waves", DM_SETTING( standingWaves ), 0.0f, 3.0f, "",
		  "Stationary waves over riffles: the wave stands where its speed against the current equals the speed of the water; "
		  "stronger on shallow fast water (by the Froude number); 0 - none", 0, "surface" },
		{ "sparkle", "Sparkle", DM_SETTING( sparkle ), 0.0f, 1.0f, "",
		  "Share of the sun (moon) highlight that breaks into sparkles: tiny ripple facets smaller than a pixel reflect the "
		  "light one by one; 0 - a smooth highlight", 0, "surface" },
		{ "sparkle_roughness", "Sparkle roughness", DM_SETTING( sparkleRoughness ), 0.05f, 1.0f, "",
		  "Roughness of the tiny ripple facets: how wide the sparkles spread around the reflection of the sun", 0, "surface" },
		{ "sparkle_size", "Sparkle size", DM_SETTING( sparkleSize ), 0.001f, 0.1f, "m", "Size of a ripple facet that reflects the sun",
		  0, "surface" },
		{ "foam_speed", "Foam speed", DM_SETTING( foamSpeed ), 0.1f, 5.0f, "m/s", "", 0, "surface" },
		{ "foam_shear", "Foam shear", DM_SETTING( foamShear ), 0.1f, 10.0f, "1/s", "", 0, "surface" },
		{ "foam_lifetime", "Foam lifetime", DM_SETTING( foamLifetime ), 0.1f, 30.0f, "s",
		  "The flow carries foam downstream; on fast water (from Foam speed) it fades in this time", 0, "surface" },
		{ "foam_still_lifetime", "Foam still lifetime", DM_SETTING( foamStillLifetime ), 0.1f, 120.0f, "s",
		  "In still and slow water foam lasts longer: rafts of foam drift into pools, backwaters and lakes", 0, "surface" },
		{ "foam_fall_slope", "Fall slope", DM_SETTING( foamFallSlope ), 0.1f, 2.0f, "",
		  "A fall from a step: the water surface along the flow steeper than this, m per m", 0, "surface" },
		{ "foam_fall_start", "Fall foam start", DM_SETTING( foamFallStart ), 0.0f, 1.0f, "m",
		  "Foam appears this far below the lip of a fall (by height): above it the water is clear (fast water and stones too), "
		  "below it foams more and more toward the foot; a lower fall does not foam", 0, "surface" },
		{ "foam_fall", "Fall foam", DM_SETTING( foamFall ), 0.0f, 2.0f, "",
		  "Foam at the foot of a fall and in the plunge pool below it: from 1 - white water", settingClamp, "surface" },
		{ "foam_fall_height", "Fall height", DM_SETTING( foamFallHeight ), 0.05f, 2.0f, "m",
		  "A fall this high gives the full Fall foam, a lower one - less", 0, "surface" },
		{ "foam_plunge_length", "Plunge length", DM_SETTING( foamPlungeLength ), 0.0f, 2.0f, "m",
		  "Below a fall the foam fades out over this distance from its foot", 0, "surface" },
		{ "foam_texture_repeat", "Foam texture repeat", DM_SETTING( foamTextureRepeat ), 0.1f, 4.0f, "m",
		  "The foam lace pattern repeats every so many metres across the flow; along it the pattern is longer, like the ripple",
		  0, "surface" },
		{ "foam_softness", "Foam softness", DM_SETTING( foamSoftness ), 0.05f, 1.0f, "",
		  "Width of the foam texture edge: thin foam strands are translucent", 0, "surface" },
		{ "foam_dissolve", "Foam dissolve", DM_SETTING( foamDissolve ), 0.0f, 1.0f, "",
		  "Where the foam fades out it dissolves in pieces at different distances (the threshold is shifted by the noise of the "
		  "foam texture); 0 - along a line", 0, "surface" },
		{ "foam_dense", "Foam dense", DM_SETTING( foamDense ), 0.0f, 1.0f, "",
		  "From this foam share the foam lace turns into dense white water (fully at 1): spillways, pools under the steps; "
		  "1 - lace only", 0, "surface" },
		{ "foam_float_cover", "Floating foam", DM_SETTING( foamFloatCover ), 0.0f, 1.0f, "",
		  "In slow water (pools, eddies) the foam is what the current brought: lace and patches covering at most this share of "
		  "the surface, no dense white water; on fast water (from Foam speed) - full", settingClamp, "surface" },
		{ "standing_crest", "Standing crest", DM_SETTING( standingCrest ), 0.0f, 1.0f, "",
		  "Share of long standing-wave crests across the whole channel (by the phase along the stream axis; near the camera they "
		  "bend the surface itself); the rest are short crests on the ripple tiles", 0, "surface" },
		{ "detail_distance", "Detail distance", DM_SETTING( detailDistance ), 0.0f, 1000.0f, "m",
		  "Nearer the water surface of streams is drawn by the detail tiles (0.25 m): farther the 1 m grid differs by less than a "
		  "pixel, and a detail tile has 16 times as many triangles; 0 - the 1 m grid only", 0, "surface" },
	},
	// Разделы окна
	{ { "time_scale", nullptr }, { "absorption", "Color" }, { "roughness", "Ripples" }, { "refraction", "Color" }, { "ripple_stretch", "Ripples" }, { "sparkle", "Sparkle" }, { "foam_speed", "Foam" }, { "detail_distance", "Detail water" } } );
	return schema;
}

const SettingsSchema<WaterSimulationSettings::Flow>& waterFlowSchema()
{
	using Settings = WaterSimulationSettings::Flow;
	// Течение — установившийся расчёт при загрузке: правка — расчёт заново
	const std::string recomputed = "; the flow of streams and lakes is recomputed on change (static water)";
	static const SettingsSchema<Settings> schema = {
		{ "flow_settle_time", "Settle time", DM_SETTING( settleTime ), 0.0f, 3600.0f, "s",
		  "Flow of streams and lakes is computed this long towards its steady state" + recomputed },
		{ "flow_time_step", "Time step", DM_SETTING( timeStep ), 0.1f, 2.0f, "s", "Step of the flow computation" + recomputed },
		{ "flow_viscosity", "Viscosity", DM_SETTING( viscosity ), 0.0f, 1.0f, "m2/s",
		  "Eddy viscosity: a wider, shorter jet of an inflowing stream" + recomputed },
		{ "flow_manning", "Bed roughness", DM_SETTING( manning ), 0.0f, 0.1f, "s/m^(1/3)",
		  "Manning roughness of lake beds: an inflowing jet fades sooner at more; streams take the Manning roughness of the "
		  "channels" + recomputed },
		{ "flow_layer", "Surface layer", DM_SETTING( layer ), 0.1f, 5.0f, "m",
		  "Depth of the flowing layer: a stream spreads over a lake near the surface" + recomputed },
		{ "flow_iterations", "Pressure iterations", DM_SETTING( iterations ), 1.0f, 100.0f, "", "Pressure solver iterations per step" + recomputed },
		{ "flow_detail_settle_time", "Detail settle time", DM_SETTING( detailSettleTime ), 0.0f, 600.0f, "s",
		  "Flow on the detail grid of the streams (0.25 m) is computed this long from the 1 m flow: around the stones, faster at the "
		  "axis than at the banks; 0 - the 1 m flow" + recomputed },
	};
	return schema;
}

const SettingsSchema<WaterSimulationSettings::NearFlow>& waterNearFlowSchema()
{
	using Settings = WaterSimulationSettings::NearFlow;
	static const SettingsSchema<Settings> schema = {
		{ "near_enabled", "Enabled", DM_SETTING( enabled ), 0.0f, 0.0f, "",
		  "Flow around the camera on a fine grid every frame: around the stones, in the bends, with eddies" },
		{ "near_shallow", "Shallow water", DM_SETTING( shallow ), 0.0f, 0.0f, "",
		  "The window solves the shallow water equations: the water height varies, and waves, a bulge in front of a stone, a "
		  "hydraulic jump below a step and reflections from the banks come by themselves; off - the flow under a rigid lid with "
		  "the surface from its pressure (Steps per frame, Pressure iterations, Viscosity, Eddies, Obstacle passage and Surface "
		  "relief are for it)" },
		{ "near_stream_roughness", "Stream roughness", DM_SETTING( streamRoughness ), 0.01f, 0.1f, "s/m^(1/3)",
		  "Shallow water: Manning roughness of the stream bed in the window. The channels use a larger one (Manning of Channels: "
		  "pebbles plus the form drag of stones and steps); in the window the stones and steps are the bed itself, so only the grain "
		  "is left: more - slower water", settingClamp },
		{ "near_stone_drag", "Stone drag", DM_SETTING( stoneDrag ), 0.0f, 100.0f, "1/m",
		  "Shallow water: a stone is solid up to half the depth and porous above it, this is its drag on the water passing "
		  "through: more - the water goes around the stone and rises in front of it, less - it seeps through a row of stones",
		  settingClamp },
		{ "near_size", "Size", DM_SETTING( size ), 2.0f, 40.0f, "m", "Side of the window around the camera", settingClamp },
		{ "near_cell", "Cell", DM_SETTING( cell ), 0.03f, 0.25f, "m", "Side of a cell of the fine grid", settingClamp },
		{ "near_steps", "Steps per frame", DM_SETTING( steps ), 1.0f, 8.0f, "", "Flow steps per frame (the game time of the frame)", settingClamp },
		{ "near_iterations", "Pressure iterations", DM_SETTING( iterations ), 1.0f, 60.0f, "", "Pressure solver iterations per step",
		  settingClamp },
		{ "near_viscosity", "Viscosity", DM_SETTING( viscosity ), 0.0f, 0.1f, "m2/s",
		  "Eddy viscosity of the fine grid: smaller eddies at less", settingClamp },
		{ "near_passage", "Obstacle passage", DM_SETTING( passage ), 0.0f, 1.0f, "",
		  "A stone above the water lets this share of the depth through: the water goes around it, and a row of stones "
		  "without a gap does not dam the stream - it seeps", settingClamp },
		{ "near_eddies", "Eddies", DM_SETTING( eddies ), 0.0f, 10.0f, "1/s",
		  "Vorticity confinement: eddies behind the stones and in the bends keep their spin", settingClamp },
		{ "near_surface", "Surface relief", DM_SETTING( surface ), 0.0f, 4.0f, "",
		  "The surface in the window follows the flow pressure: a bulge in front of a stone, a dip behind it and over it; "
		  "0 - flat", settingClamp },
		{ "near_foam", "Foam", DM_SETTING( foam ), 0.0f, 1.0f, "",
		  "Share of the window's own foam: born where the water hits a stone and on the velocity shear, carried by the window's "
		  "flow every frame; 0 - the foam field of the 0.25 m grid", settingClamp },
		{ "near_pattern", "Pattern", DM_SETTING( pattern ), 0.0f, 1.0f, "",
		  "Share of the foam pattern carried by the window's flow: it streams around the stones, stretches into streaks and curls "
		  "in the eddies; 0 - the pattern tiles", settingClamp },
		{ "near_pattern_period", "Pattern period", DM_SETTING( patternPeriod ), 0.3f, 10.0f, "s",
		  "A layer of the carried pattern starts anew this often: longer - longer streaks, but the pattern gets torn apart", settingClamp },
		{ "near_pattern_stretch", "Pattern stretch", DM_SETTING( patternStretch ), 1.2f, 10.0f, "",
		  "A layer of the carried pattern stretched (or squeezed) this many times weighs half: the other layer takes over, and the "
		  "pattern does not draw out into ribbons", settingClamp },
	};
	return schema;
}

const SettingsSchema<WaterChannelsSettings>& waterChannelsSchema()
{
	using Settings = WaterChannelsSettings;
	// Параметры генератора кривых (группа streamGeneratorGroup) применяются при следующей загрузке, остальные перестраивают
	// русла сразу — к подсказке
	const std::string rebuilt = "; the channels are rebuilt on change";
	const std::string generated = "; generates the stream curves anew at the next level load";
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "min_discharge", "Min discharge", DM_SETTING( minDischarge ), 0.0005f, 0.05f, "m3/s",
		  "A channel starts where the discharge is larger" + generated, 0, streamGeneratorGroup },
		{ "width_coef", "Width coef", DM_SETTING( widthCoef ), 0.5f, 10.0f, "", "Bed width w = a * Q^0.5, m" + rebuilt },
		{ "min_width", "Min width", DM_SETTING( minWidth ), 0.5f, 10.0f, "m", "Narrowest bed" + rebuilt },
		{ "depth_coef", "Depth coef", DM_SETTING( depthCoef ), 0.1f, 5.0f, "", "Incision d = c * Q^0.4, m" + rebuilt },
		{ "min_incision", "Min incision", DM_SETTING( minIncision ), 0.0f, 3.0f, "m", "Shallowest incision" + rebuilt },
		{ "bank_slope", "Bank slope", DM_SETTING( bankSlope ), 0.2f, 5.0f, "", "Bank: horizontal metres per metre of incision" + rebuilt },
		{ "meander_length", "Meander length", DM_SETTING( meanderLength ), 5.0f, 200.0f, "m",
		  "Meander wavelength along the stream" + generated, 0, streamGeneratorGroup },
		{ "meander_amplitude", "Meander amplitude", DM_SETTING( meanderAmplitude ), 0.0f, 10.0f, "m",
		  "Lateral shift at Q >= 0.1 m3/s" + generated, 0, streamGeneratorGroup },
		{ "meander_max_slope", "Meander max slope", DM_SETTING( meanderMaxSlope ), 0.005f, 0.3f, "",
		  "Steeper terrain has no meanders, m per m" + generated, 0, streamGeneratorGroup },
		{ "min_slope", "Min slope", DM_SETTING( minSlope ), 0.0f, 0.05f, "", "Bed drop downstream at least, m per m" + rebuilt },
		{ "lake_depth", "Lake depth", DM_SETTING( lakeDepth ), 0.01f, 1.0f, "m",
		  "Deeper filled depressions are lakes: not carved, streams end there" + generated, 0, streamGeneratorGroup },
		{ "min_lake_area", "Min lake area", DM_SETTING( minLakeArea ), 0.0f, 1000.0f, "m2",
		  "Smaller depressions are a part of the stream bed, not lakes" + generated, 0, streamGeneratorGroup },
		{ "thalweg", "Thalweg", DM_SETTING( thalweg ), 0.0f, 1.0f, "",
		  "Extra incision in the bed middle, share of the incision" + rebuilt },
		{ "manning", "Manning", DM_SETTING( manning ), 0.02f, 0.15f, "s/m^(1/3)",
		  "Stream bed roughness: water depth of the ribbons" + rebuilt },
		{ "min_water_slope", "Min water slope", DM_SETTING( minWaterSlope ), 0.001f, 0.05f, "",
		  "Slope in the water depth at least, m per m" + rebuilt },
		{ "min_speed", "Min speed", DM_SETTING( minSpeed ), 0.0f, 2.0f, "m/s", "Stream flow at least" + rebuilt },
		{ "min_water_depth", "Min water depth", DM_SETTING( minWaterDepth ), 0.05f, 1.0f, "m", "Water over the bed at least" + rebuilt },
		{ "freeboard", "Freeboard", DM_SETTING( freeboard ), 0.0f, 2.0f, "m",
		  "Water stands this much below the bank top: the bed is cut for floods" + rebuilt },
		{ "source_taper", "Source taper", DM_SETTING( sourceTaper ), 0.0f, 100.0f, "m",
		  "From a spring the narrowest bed, incision, water and freeboard grow over this length" + rebuilt },
		{ "bed_shape", "Bed shape", DM_SETTING( bedShape ), 1.0f, 12.0f, "",
		  "Bed cross-section: 2 - V (sharp thalweg), more - flat U" + rebuilt },
		{ "bank_roundness", "Bank roundness", DM_SETTING( bankRoundness ), 0.0f, 1.0f, "",
		  "1 - rounded bank top and toe, 0 - straight bank with a sharp edge" + rebuilt },
		{ "bend_asymmetry", "Bend asymmetry", DM_SETTING( bendAsymmetry ), 0.0f, 1.0f, "",
		  "Outer bank of a bend deeper and steeper, inner - a gentle bar" + rebuilt },
		{ "pool_depth", "Pool depth", DM_SETTING( poolDepth ), 0.0f, 1.0f, "",
		  "Pools deeper by this share of the incision (0 - no pools)" + rebuilt },
		{ "pool_spacing", "Pool spacing", DM_SETTING( poolSpacing ), 2.0f, 15.0f, "widths",
		  "Pool and riffle every this many channel widths" + rebuilt },
		{ "bed_noise", "Bed noise", DM_SETTING( bedNoise ), 0.0f, 0.5f, "m", "Unevenness of the bed and banks" + rebuilt },
		{ "bed_noise_scale", "Bed noise scale", DM_SETTING( bedNoiseScale ), 0.3f, 5.0f, "m", "Size of the bed unevenness" + rebuilt },
		{ "step_height", "Step height", DM_SETTING( stepHeight ), 0.0f, 2.0f, "m",
		  "Steep reaches are stepped (step-pool, cascade): drop of a step, a pool below it (0 - no steps)" + rebuilt },
		{ "step_steepness", "Step steepness", DM_SETTING( stepSteepness ), 0.5f, 2.5f, "",
		  "Step height per step spacing in units of the reach slope (1-1.5 in nature): the spacing" + rebuilt },
		{ "step_min_slope", "Step min slope", DM_SETTING( stepMinSlope ), 0.005f, 0.2f, "",
		  "Gentler reaches have no steps (pools and riffles), m per m" + rebuilt },
		{ "lake_depth_ratio", "Lake depth ratio", DM_SETTING( lakeDepthRatio ), 0.0f, 0.3f, "",
		  "Deepest lake point per square root of the lake area" + rebuilt },
		{ "lake_min_depth", "Lake min depth", DM_SETTING( lakeMinDepth ), 0.0f, 10.0f, "m",
		  "Deepest point of a small lake at least" + rebuilt },
		{ "lake_max_depth", "Lake max depth", DM_SETTING( lakeMaxDepth ), 0.0f, 50.0f, "m",
		  "Deepest point of a large lake at most" + rebuilt },
		{ "lake_shelf_width", "Lake shelf width", DM_SETTING( lakeShelfWidth ), 0.0f, 20.0f, "m",
		  "Shallow shelf along the shore" + rebuilt },
		{ "lake_shelf_depth", "Lake shelf depth", DM_SETTING( lakeShelfDepth ), 0.0f, 3.0f, "m",
		  "Water depth at the shelf edge" + rebuilt },
		{ "lake_drop_slope", "Lake drop slope", DM_SETTING( lakeDropSlope ), 0.05f, 2.0f, "",
		  "Drop-off beyond the shelf, m per m" + rebuilt },
		{ "lake_freeboard", "Lake freeboard", DM_SETTING( lakeFreeboard ), 0.0f, 1.0f, "m",
		  "Lake water stands this much below the spill level of its basin: the shore is above the water, the outlet cuts the sill" + rebuilt },
		{ "stone_density", "Stone density", DM_SETTING( stoneDensity ), 0.0f, 5.0f, "1/m",
		  "Stones per metre of a stream (bed and banks): denser on riffles and steep reaches" + rebuilt },
		{ "stone_size", "Stone size", DM_SETTING( stoneSize ), 0.05f, 1.0f, "m", "Typical stone diameter" + rebuilt },
		{ "bank_stones", "Bank stones", DM_SETTING( bankStones ), 0.0f, 1.0f, "",
		  "Share of the stones at the banks, near the water edge" + rebuilt },
		{ "boulder_density", "Boulder density", DM_SETTING( boulderDensity ), 0.0f, 0.5f, "1/m",
		  "Boulders per metre of a stream: more on steep reaches" + rebuilt },
		{ "boulder_size", "Boulder size", DM_SETTING( boulderSize ), 0.3f, 3.0f, "m", "Boulder diameter" + rebuilt },
		{ "smooth", "Smooth passes", DM_SETTING( smooth ), 0.0f, 20.0f, "", "Smoothing passes of the channel axis along the flow" + generated, 0,
		  streamGeneratorGroup },
	},
	// Разделы окна
	{ { "min_discharge", "Generator" }, { "width_coef", "Bed" }, { "meander_length", "Generator" }, { "min_slope", "Bed" }, { "lake_depth", "Generator" }, { "thalweg", "Bed" }, { "manning", "Water" }, { "freeboard", "Bed" }, { "step_height", "Steps" }, { "lake_depth_ratio", "Lake bed" }, { "stone_density", "Stones" }, { "smooth", "Generator" } } );
	return schema;
}

const SettingsSchema<StreamCurve>& streamCurveSchema()
{
	using Settings = StreamCurve;
	const std::string rebuilt = "; the channels are rebuilt on change";
	static const SettingsSchema<Settings> schema = {
		{ "enabled", "Enabled", DM_SETTING( enabled ), 0.0f, 0.0f, "", "Off - no channel and no water" + rebuilt },
		{ "width_scale", "Width scale", DM_SETTING( widthScale ), 0.1f, 4.0f, "", "Bed width of this stream, times the common one" + rebuilt },
		{ "depth_scale", "Depth scale", DM_SETTING( depthScale ), 0.1f, 4.0f, "", "Incision of this stream, times the common one" + rebuilt },
		{ "freeboard_scale", "Freeboard scale", DM_SETTING( freeboardScale ), 0.1f, 4.0f, "", "Bank above the water, times the common one" + rebuilt },
		{ "bank_slope_scale", "Bank slope scale", DM_SETTING( bankSlopeScale ), 0.1f, 4.0f, "", "Gentler (> 1) or steeper (< 1) banks" + rebuilt },
		{ "thalweg_scale", "Thalweg scale", DM_SETTING( thalwegScale ), 0.1f, 4.0f, "", "Deeper middle of the bed, times the common one" + rebuilt },
		{ "roughness_scale", "Roughness scale", DM_SETTING( roughnessScale ), 0.1f, 4.0f, "",
		  "Bed roughness (Manning): deeper, slower water at > 1" + rebuilt },
		{ "discharge_scale", "Discharge scale", DM_SETTING( dischargeScale ), 0.1f, 4.0f, "",
		  "Water of this stream: wider, deeper bed at > 1" + rebuilt },
		{ "stone_scale", "Stone scale", DM_SETTING( stoneScale ), 0.1f, 4.0f, "",
		  "Stones and boulders of this stream, times the common density" + rebuilt },
	};
	return schema;
}

const SettingsSchema<ParticleEmitterSettings>& particleEmitterSchema()
{
	using Settings = ParticleEmitterSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		// Место экземпляра — колонка position строки LevelParticleEmitters (её пишет «Save level» отдельно); в окне — у эмиттера
		// в точке и в шаре
		{ "", "Position", DM_SETTING( position ), 0.0f, 0.0f, "m", "Where the particles are born (the viewport gizmo moves it)",
		  settingDrag, "placed" },
		// Единица частоты и подсказка радиуса — по способу рождения (ParticleSystem)
		{ "rate", "Rate", DM_SETTING( rate ), 0.0f, 10.0f, "1/s", "", settingClampLow | settingWideHigh },
		{ "radius", "Radius", DM_SETTING( radius ), 0.0f, 10.0f, "m", "", settingClampLow | settingWideHigh },
		{ "height_min", "Height min", DM_SETTING( heightMin ), -10.0f, 50.0f, "m",
		  "Above the terrain (camera) or the water surface (water, fall; below zero for fall - born under water and rises)" },
		{ "height_max", "Height max", DM_SETTING( heightMax ), -10.0f, 50.0f, "m" },
		// Маска, спрайт и ёмкость пула задают ресурсы эмиттера — только в базе
		{ "mask", nullptr, DM_SETTING( mask ) },
		{ "sprite", nullptr, DM_SETTING( sprite ) },
		{ "max_particles", nullptr, DM_SETTING( maxParticles ) },
		{ "lifetime_min", "Lifetime min", DM_SETTING( lifetimeMin ), 0.0f, 10.0f, "s", "", settingClampLow | settingWideHigh },
		{ "lifetime_max", "Lifetime max", DM_SETTING( lifetimeMax ), 0.0f, 10.0f, "s", "", settingClampLow | settingWideHigh },
		{ "size_start", "Size start", DM_SETTING( sizeStart ), 0.001f, 0.1f, "m", "", settingWideHigh },
		{ "size_end", "Size end", DM_SETTING( sizeEnd ), 0.001f, 0.1f, "m", "", settingWideHigh },
		{ "grow_time", "Grow time", DM_SETTING( growTime ), 0.0f, 2.0f, "s",
		  "From birth the size grows from Birth size to Size start in this time (0 - Size start at once), then to Size end" },
		{ "birth_size", "Birth size", DM_SETTING( birthSize ), 0.0f, 0.1f, "m", "", settingWideHigh },
		{ "color", "Color", DM_SETTING( color ), 0.0f, 0.0f, "", "", settingColor },
		{ "alpha", "Alpha", DM_SETTING( alpha ), 0.0f, 1.0f },
		{ "fade_in", "Fade in", DM_SETTING( fadeIn ), 0.0f, 1.0f, "", "Share of the life" },
		{ "fade_out", "Fade out", DM_SETTING( fadeOut ), 0.0f, 1.0f, "", "Share of the life" },
		{ "velocity", "Velocity", DM_SETTING( velocity ), -20.0f, 20.0f, "m/s", "Initial velocity", settingDrag },
		{ "velocity_spread", "Velocity spread", DM_SETTING( velocitySpread ), 0.0f, 10.0f, "m/s" },
		{ "gravity", "Gravity", DM_SETTING( gravity ), 0.0f, 20.0f, "m/s2" },
		{ "drag", "Drag", DM_SETTING( drag ), 0.0f, 20.0f, "1/s", "Velocity tends to the air (wind) and water velocity" },
		{ "wind", "Wind", DM_SETTING( wind ), 0.0f, 2.0f, "", "Share of the level wind" },
		{ "curl", "Curl", DM_SETTING( curl ), 0.0f, 5.0f, "m/s", "Strength of the curl noise vortices" },
		{ "curl_scale", "Curl scale", DM_SETTING( curlScale ), 0.1f, 50.0f, "m", "Size of the vortices", settingLogarithmic | settingClampLow },
		{ "water_flow", "Water flow", DM_SETTING( waterFlow ), 0.0f, 2.0f, "", "Share of the water velocity under the particle" },
		{ "water_speed", "Water speed", DM_SETTING( waterSpeed ), 0.0f, 5.0f, "m/s", "Spawn on water faster than this" },
		{ "spray_height_min", "Spray height min", DM_SETTING( sprayHeightMin ), 0.0f, 0.5f, "m",
		  "Churn (spawn fall): it floats this height over its float level at least (born under water - rises there with Velocity y) "
		  "and drifts there with the flow" },
		{ "spray_height_max", "Spray height max", DM_SETTING( sprayHeightMax ), 0.0f, 0.5f, "m", "And at most" },
		{ "self_shadow", "Self shadow", DM_SETTING( selfShadow ), 0.0f, 10.0f, "",
		  "Froth with a sprite: density of its mass for the sun - the side away from the sun is shaded (0 - none)" },
		{ "sink", "Sink", DM_SETTING( sink ), 0.0f, 1.5f, "",
		  "Churn (spawn fall): a surfaced clump keeps its centre this share of its half size below the water (the clump faces the "
		  "camera: 0 - centre on the water, 1 - top on the water), at most half the water depth down" },
		{ "hold", "Hold", DM_SETTING( hold ), 0.0f, 3.0f, "s",
		  "Churn (spawn fall): for this long after birth the flow does not carry the clump - it stays at the foot of the fall" },
		{ "flatten", "Flatten", DM_SETTING( flatten ), 0.05f, 1.0f, "",
		  "Froth: the clump is squashed vertically to this share (a churning mass is wide and low; 1 - the camera-facing quad as is)",
		  settingClamp },
		{ "core_occlusion", "Core occlusion", DM_SETTING( coreOcclusion ), 0.0f, 1.0f, "",
		  "Froth with a sprite: share of the sky light its dense core does not get (0 - lit everywhere)", settingClamp },
		{ "collide", "Collide", DM_SETTING( collide ), 0.0f, 0.0f, "", "With the terrain: needles lie down, dots die" },
		{ "transmission", "Transmission", DM_SETTING( transmission ), 0.0f, 4.0f, "", "Glow against the sun (pollen, fluff)" },
		{ "emissive", "Emissive", DM_SETTING( emissive ), 0.0f, 10.0f, "cd/m2", "", settingWideHigh },
	},
	// Разделы окна
	{ { "Position", "Spawn" }, { "lifetime_min", "Lifetime" }, { "size_start", "Look" }, { "fade_in", "Lifetime" }, { "velocity", "Motion" }, { "transmission", "Look" } } );
	return schema;
}

const SettingsSchema<ReflectionCaptureSettings>& reflectionCaptureSchema()
{
	using Settings = ReflectionCaptureSettings;
	// Точка и размер грани — съёмка заново, когда правка закончена; радиус, переход и яркость — сразу, без съёмки
	static const SettingsSchema<Settings> schema = {
		{ "position", "Position", DM_SETTING( position ), 0.0f, 0.0f, "m",
		  "Capture point (a typical eye height above the water); recaptured when the edit ends", settingDrag },
		{ "influence_radius", "Influence radius", DM_SETTING( influenceRadius ), 10.0f, 500.0f, "m",
		  "Inside it the water reflects the map (the reflected ray is traced through its distances) instead of the sky",
		  settingClampLow | settingWideHigh },
		{ "transition", "Transition", DM_SETTING( transition ), 0.01f, 1.0f, "",
		  "Share of the radius at its edge where the map fades into the sky" },
		{ "face_size", "Face size", DM_SETTING( faceSize ), 0.0f, 0.0f, "texels", "Side of a cube face; recaptured", 0, "",
		  { 64, 128, 256, 512 } },
		{ "brightness", "Brightness", DM_SETTING( brightness ), 0.0f, 4.0f, "", "Multiplier of the map" },
	};
	return schema;
}

const SettingsSchema<AmbientSoundSettings>& ambientSoundSchema()
{
	using Settings = AmbientSoundSettings;
	// Громкости — доли амплитуды; частоты — середины полос шума, по логарифму (слух — по октавам)
	static const SettingsSchema<Settings> schema = {
		{ "volume", "Volume", DM_SETTING( volume ), 0.0f, 2.0f, "", "All ambient sounds of the level" },
		{ "wind_enabled", "Enabled", DM_SETTING( windEnabled ), 0.0f, 0.0f, "", "Off - the wind is silent and keeps no voices", 0,
		  "Wind" },
		{ "wind_volume", "Volume", DM_SETTING( windVolume ), 0.0f, 2.0f, "", "Air rushing past the camera", 0, "Wind" },
		{ "wind_reference_speed", "Reference speed", DM_SETTING( windReferenceSpeed ), 1.0f, 30.0f, "m/s",
		  "Wind speed at the camera with the full volume (the speed of the level wind times the terrain field and the gust)",
		  settingClampLow, "Wind" },
		{ "wind_frequency", "Frequency", DM_SETTING( windFrequency ), 30.0f, 1000.0f, "Hz",
		  "Middle of the rumble band in a calm; it rises with the wind speed", settingLogarithmic | settingClampLow, "Wind" },
		{ "grass_enabled", "Enabled", DM_SETTING( grassEnabled ), 0.0f, 0.0f, "", "Off - the grass is silent and keeps no voices",
		  0, "Grass" },
		{ "grass_volume", "Volume", DM_SETTING( grassVolume ), 0.0f, 2.0f, "", "Rustle of the grass around the camera", 0, "Grass" },
		{ "grass_radius", "Radius", DM_SETTING( grassRadius ), 1.0f, 20.0f, "m", "Grass masks are averaged in this circle",
		  settingClampLow, "Grass" },
		{ "grass_height", "Height", DM_SETTING( grassHeight ), 3.0f, 50.0f, "m",
		  "Camera this high above the ground does not hear the grass (quieter from 2.5 m)", settingClampLow, "Grass" },
		{ "grass_frequency", "Frequency", DM_SETTING( grassFrequency ), 500.0f, 12000.0f, "Hz", "Middle of the rustle band",
		  settingLogarithmic | settingClampLow, "Grass" },
		{ "crown_enabled", "Enabled", DM_SETTING( crownEnabled ), 0.0f, 0.0f, "", "Off - the crowns are silent and keep no voices",
		  0, "Crowns" },
		{ "crown_volume", "Volume", DM_SETTING( crownVolume ), 0.0f, 2.0f, "", "Rustle of the forest crowns around", 0, "Crowns" },
		{ "crown_reference_strength", "Reference strength", DM_SETTING( crownReferenceStrength ), 0.05f, 2.0f, "",
		  "Strength of the level wind (Wind/Strength - how much it bends the trees) with the full volume: the crowns get louder "
		  "with it", settingClampLow, "Crowns" },
		{ "crown_radius", "Radius", DM_SETTING( crownRadius ), 10.0f, 300.0f, "m",
		  "Forest masks in this circle, by four sides: the sound comes from where the forest is", settingClampLow, "Crowns" },
		{ "crown_falloff", "Falloff", DM_SETTING( crownFalloff ), 2.0f, 100.0f, "m",
		  "Forest this far away is heard at half, farther - quieter by the distance squared: the crowns are heard in the forest "
		  "and at its edge, not across a meadow", settingClampLow, "Crowns" },
		{ "crown_gust_swing", "Gust swing", DM_SETTING( crownGustSwing ), 0.0f, 40.0f, "dB",
		  "In a lull between the gusts the crowns are this much quieter than in a gust", settingClamp, "Crowns" },
		{ "crown_frequency", "Frequency", DM_SETTING( crownFrequency ), 200.0f, 8000.0f, "Hz", "Middle of the rustle band",
		  settingLogarithmic | settingClampLow, "Crowns" },
		{ "stream_enabled", "Enabled", DM_SETTING( streamEnabled ), 0.0f, 0.0f, "", "Off - the streams are silent and keep no voices",
		  0, "Streams" },
		{ "stream_volume", "Volume", DM_SETTING( streamVolume ), 0.0f, 2.0f, "",
		  "Babble of the streams: a noisy place of the level water (louder than 95 % of places) 3 m away; louder over steps, quieter "
		  "behind a ridge", 0, "Streams" },
		{ "stream_voices", "Voices", DM_SETTING( streamVoices ), 1.0f, 16.0f, "",
		  "Places of water heard at once (the loudest at the camera); the rest are silent", settingClamp, "Streams" },
		{ "stream_distance", "Distance", DM_SETTING( streamDistance ), 20.0f, 1000.0f, "m", "Water farther than this is not heard",
		  settingClampLow, "Streams" },
		{ "stream_rapids_from", "Rapids from", DM_SETTING( streamRapidsFrom ), 0.0f, 1.0f, "",
		  "A place of water this steep (0 - flat, 1 - a step of the stream) plays the rapids recording, a flatter one - the calm "
		  "stream", settingClamp, "Streams" },
		// Записи — Tools/ambient_sound.py (громкость записи — окно «Sounds»): строкового свойства у окна нет
		{ "stream_sound", nullptr, DM_SETTING( streamSound ) },
		{ "stream_rapids_sound", nullptr, DM_SETTING( streamRapidsSound ) },
	};
	return schema;
}

const SettingsSchema<SoundscapeLayerSettings>& soundscapeLayerSchema()
{
	using Settings = SoundscapeLayerSettings;
	static const SettingsSchema<Settings> schema = withCategories( SettingsSchema<Settings>{
		{ "enabled", "Enabled", DM_SETTING( enabled ), 0.0f, 0.0f, "", "Off - the layer does not sing" },
		// Записи и места — флажками подокон «Sounds» и «Habitat»: строкового свойства у окна нет
		{ "sound", nullptr, DM_SETTING( sound ) },
		{ "habitat", nullptr, DM_SETTING( habitat ) },
		// Когда поёт — правила слоя (SoundRules, подокно «Rules»), не поля строки
		{ "singers", "Singers", DM_SETTING( singers ), 0.0f, 8.0f, "",
		  "Singing at once at the full activity; the rules of the layer (hours …) scale it", settingClamp },
		{ "distance_from,distance_to", "Distance", DM_SETTING( distance ), 5.0f, 300.0f, "m", "From the listener",
		  settingRangeControl },
		{ "height_from,height_to", "Height", DM_SETTING( height ), 0.0f, 100.0f, "m", "Above the ground", settingRangeControl },
		{ "bout_from,bout_to", "Bout", DM_SETTING( bout ), 5.0f, 600.0f, "s",
		  "Sings this long from one place, then flies away and another one comes", settingRangeControl },
		{ "pause_from,pause_to", "Pause", DM_SETTING( pause ), -10.0f, 60.0f, "s",
		  "Between the phrases; below 0 - the next phrase starts that much before the end of the previous one and they crossfade "
		  "(a chorus recording looped without a seam)", settingRangeControl },
		{ "arrival_from,arrival_to", "Arrival", DM_SETTING( arrival ), 0.0f, 600.0f, "s",
		  "A singer left - the next one comes after this long (a cuckoo calls in sessions minutes apart)", settingRangeControl },
		{ "volume", "Volume", DM_SETTING( volume ), 0.0f, 2.0f, "", "Of a singer 10 m away" },
		{ "pitch", "Pitch spread", DM_SETTING( pitch ), 0.0f, 0.3f, "", "Each singer is higher or lower by up to this share" },
	},
	// Разделы окна
	{ { "singers", "Placement" }, { "bout_from,bout_to", "Timing" }, { "volume", "Sound" } } );
	return schema;
}

const SettingsSchema<SoundRecord>& soundRecordSchema()
{
	using Settings = SoundRecord;
	static const SettingsSchema<Settings> schema = {
		{ "volume", "Volume", DM_SETTING( volume ), 0.0f, 4.0f, "",
		  "Gain of the recording in every layer that sings it (a quiet recording is levelled with the others here, the layer "
		  "volume sets the singer)" },
	};
	return schema;
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
