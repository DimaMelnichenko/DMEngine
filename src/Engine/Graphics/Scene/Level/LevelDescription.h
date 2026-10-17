#pragma once

#include <optional>
#include <string>
#include <vector>
#include "DirectX.h"
#include "LevelSettings.h"
#include "Light\DMLight.h"

// Состав уровня из base.db3: строка таблицы Levels и то, на что она ссылается. Только данные: читает их LibraryLoader,
// а Scene создаёт по ним объекты. Пустой optional — у уровня этого нет (колонка NULL)
struct LevelDescription
{
	// Экземпляр модели на уровне (строка LevelModels): одна модель может стоять в нескольких местах
	struct ModelInstance
	{
		uint32_t id = 0;	// строка LevelModels — куда пишет «Save level»
		uint32_t model = 0;
		DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
		DirectX::XMFLOAT4 rotation = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f );	// кватернион x, y, z, w, как rotation узла glTF
		DirectX::XMFLOAT3 scale = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );
	};

	// Модель слоя расстановки, её вес и тень (ScatterLayerModels)
	struct ScatterModel
	{
		uint32_t id = 0;	// строка ScatterLayerModels
		uint32_t model = 0;
		float weight = 1.0f;
		bool castShadow = true;
		// Свои границы LOD модели у слоя, м (ScatterLayerModels.lod_distances: «6,14»); пусто — дальности модели
		std::vector<float> lodDistances;
		GS::ScatterVariantPlacement placement;	// свои размер, разброс, наклон и группа куртин; NULL — как у слоя
	};

	// Слой расстановки — растение: модели-варианты со всеми их LOD, маска плотности и параметры (таблица ScatterLayers)
	struct ScatterLayer
	{
		std::vector<ScatterModel> models;
		std::string mask;
		GS::ScatterLayerSettings settings;
	};

	// Набор расстановки: трава, камешки (таблица ScatterSets). Проход и отсечение граней задаёт материал слоя
	struct ScatterSet
	{
		std::string name;
		GS::ScatterSetSettings settings;	// строка ScatterSets
		std::vector<ScatterLayer> layers;
	};

	uint32_t id = 0;
	std::string name;
	std::optional<GS::TerrainDescription> terrain;	// строка таблицы Terrain и слои его материала (TerrainLayers)
	std::vector<GS::TerrainEdit> terrainEdits;	// правки его рельефа по порядку слоёв: таблицы TerrainEdits / TerrainEditPoints
	std::optional<GS::TerrainErosionSettings> terrainErosion;	// эрозия его карты высот (Terrain.erosion); NULL — нет
	GS::TerrainLayerRules terrainLayerRules;	// правила его слоёв и масок (строка TerrainLayerRules; нет — по умолчанию)
	std::vector<GS::TerrainPaintTile> terrainPaint;	// мазки кисти поверх правил (таблица TerrainPaint)
	std::optional<GS::FarTerrainSettings> farTerrain;	// дальние горы вокруг карты (Levels.far_terrain); NULL — нет
	std::optional<uint32_t> waterSimulationId;	// строка WaterSimulation: вода по рельефу; NULL — воды нет
	std::optional<GS::WaterSimulationSettings> waterSimulation;
	std::optional<uint32_t> sky;		// модель небесной сферы
	std::vector<ModelInstance> modelInstances;	// таблица LevelModels
	std::vector<ScatterSet> scatterSets;	// таблица LevelScatterSets
	std::vector<GS::ParticleEmitterSettings> particleEmitters;	// включённые строки LevelParticleEmitters
	std::vector<GS::ReflectionCaptureSettings> reflectionCaptures;	// включённые строки LevelReflectionCaptures
	std::optional<GS::AmbientSoundSettings> ambientSound;	// звуки окружения (Levels.ambient_sound); NULL — тишина

	// Свет и окружение уровня — как сущности уровня в UE (Directional / Point / Spot Light, Sky Atmosphere,
	// Post Process Volume). Строки настроек неба и постобработки — по ссылке из Levels; NULL — значения по умолчанию
	std::vector<DMLight> lights;				// таблица LevelLights
	std::optional<uint32_t> sunPositionId;		// строка SunPosition: место и время — направление солнца
	std::optional<GS::SunPositionSettings> sunPosition;
	std::optional<uint32_t> atmosphereId;		// строка SkyAtmosphere
	GS::SkyAtmosphereSettings atmosphere;
	std::vector<GS::StarRecord> stars;			// таблица Stars — звёзды фона атмосферы, общие для уровней
	std::optional<uint32_t> hdriBackdropId;		// строка HDRIBackdrop: панорама вместо атмосферы
	std::optional<GS::HDRIBackdropSettings> hdriBackdrop;
	std::optional<uint32_t> postProcessId;		// строка PostProcessSettings
	GS::PostProcessSettings postProcess;
	std::optional<uint32_t> windId;				// строка Wind: ветер уровня; NULL — ветра нет
	std::optional<GS::WindSettings> wind;
	std::optional<uint32_t> heightFogId;		// строка ExponentialHeightFog: туман уровня; NULL — тумана нет
	std::optional<GS::HeightFogSettings> heightFog;
	std::optional<uint32_t> volumetricCloudId;	// строка VolumetricCloud: облака; NULL — облаков нет
	std::optional<GS::VolumetricCloudSettings> volumetricCloud;

	// Пресет окружения (таблица LevelPresets, как снимок в Level Snapshots UE): именованный набор строк окружения уровня —
	// время суток, небо, постобработка, ветер, туман, облака. Колонка NULL — строка самого уровня. Активный пресет
	// подставляет свои строки в *Id выше: их читают окна и в них пишет «Save level»; рельеф, вода, расстановка, модели и
	// свет — общие для всех пресетов (содержимое уровня)
	struct EnvironmentRows
	{
		std::optional<uint32_t> sunPosition;
		std::optional<uint32_t> atmosphere;
		std::optional<uint32_t> postProcess;
		std::optional<uint32_t> wind;
		std::optional<uint32_t> heightFog;
		std::optional<uint32_t> volumetricCloud;
	};
	EnvironmentRows levelRows;				// строки самого уровня (Levels) — без пресета
	std::string preset;						// активный пресет; пусто — строки самого уровня
	bool presetLocked = false;				// LevelPresets.locked: «Save level» не пишет окружение (эталон проверок)
	std::vector<std::string> presets;		// пресеты уровня по имени
};
