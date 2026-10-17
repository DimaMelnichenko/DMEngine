#pragma once

#include <optional>
#include <vector>
#include "LevelDescription.h"

// Что «Save level» пишет в базу — окна уровня, собранные в данные (Scene::levelSaveData): окружение и объекты сцены.
// Снимок после загрузки и после каждого сохранения — прежнее (Scene::markLevelSaved): в строки уходят только колонки,
// которые с тех пор поменялись (RowUpdate, ObjectLibrary/SettingsDatabase.h), как несохранённые правки в редакторе UE.
// Остальное в базе не трогается: NULL (значение по умолчанию) остаётся NULL, запись скриптом — как была
struct LevelSaveData
{
	// Окружение (LibraryLoader::saveLevelEnvironment)
	std::vector<DMLight> lights;
	std::vector<uint32_t> removedLights;		// строки убранных в редакторе источников
	// Что у светила считается, а не задаётся (по номеру в lights): направление солнца и луны — от SunPosition (время
	// суток), освещённость луны — по фазе. У строки, которая уже в базе, не пишутся: при загрузке их посчитает SunPosition
	std::vector<bool> directionFromTime;
	std::vector<bool> intensityFromTime;
	std::optional<GS::SunPositionSettings> sunPosition;
	std::optional<GS::SkyAtmosphereSettings> atmosphere;	// небо — то, что у уровня работает: атмосфера или панорама
	std::optional<GS::HDRIBackdropSettings> hdri;
	GS::PostProcessSettings postProcess;
	GS::WindSettings wind;
	std::optional<GS::HeightFogSettings> heightFog;
	std::optional<GS::VolumetricCloudSettings> cloud;

	// Объекты сцены (LibraryLoader::saveLevelScene)
	std::vector<LevelDescription::ModelInstance> models;
	std::vector<uint32_t> removedModels;
	std::optional<GS::TerrainSettings> terrain;
	std::optional<GS::FarTerrainSettings> farTerrain;
	std::vector<GS::ScatterSetSettings> scatterSets;
	std::vector<GS::ScatterLayerRecord> scatterLayers;
	std::optional<GS::WaterSimulationSettings> water;
	std::vector<GS::ParticleEmitterSettings> emitters;
	std::vector<GS::ReflectionCaptureSettings> reflectionCaptures;
	std::optional<GS::AmbientSoundSettings> ambientSound;
};
