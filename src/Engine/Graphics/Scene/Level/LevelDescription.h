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
		uint32_t model = 0;
		DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
		DirectX::XMFLOAT4 rotation = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f );	// кватернион x, y, z, w, как rotation узла glTF
		DirectX::XMFLOAT3 scale = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );
	};

	// Модель слоя расстановки, её вес и тень (ScatterLayerModels)
	struct ScatterModel
	{
		uint32_t model = 0;
		float weight = 1.0f;
		bool castShadow = true;
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
		std::vector<ScatterLayer> layers;
	};

	struct Particles
	{
		std::string material;
		std::string texture;
		uint32_t countPerCell = 0;
		uint32_t areaSize = 0;
	};

	uint32_t id = 0;
	std::string name;
	std::optional<uint32_t> terrain;	// строка таблицы Terrain
	std::vector<GS::TerrainEdit> terrainEdits;	// правки его рельефа по порядку слоёв: таблицы TerrainEdits / TerrainEditPoints
	std::optional<uint32_t> sky;		// модель небесной сферы
	std::vector<ModelInstance> modelInstances;	// таблица LevelModels
	std::vector<ScatterSet> scatterSets;	// таблица LevelScatterSets
	std::optional<Particles> particles;

	// Свет и окружение уровня — как сущности уровня в UE (Directional / Point / Spot Light, Sky Atmosphere,
	// Post Process Volume). Строки настроек неба и постобработки — по ссылке из Levels; NULL — значения по умолчанию
	std::vector<DMLight> lights;				// таблица LevelLights
	std::optional<uint32_t> sunPositionId;		// строка SunPosition: место и время — направление солнца
	std::optional<GS::SunPositionSettings> sunPosition;
	std::optional<uint32_t> atmosphereId;		// строка SkyAtmosphere
	GS::SkyAtmosphereSettings atmosphere;
	std::optional<uint32_t> hdriBackdropId;		// строка HDRIBackdrop: панорама вместо атмосферы
	std::optional<GS::HDRIBackdropSettings> hdriBackdrop;
	std::optional<uint32_t> postProcessId;		// строка PostProcessSettings
	GS::PostProcessSettings postProcess;
	std::optional<uint32_t> windId;				// строка Wind: ветер уровня; NULL — ветра нет
	std::optional<GS::WindSettings> wind;
};
