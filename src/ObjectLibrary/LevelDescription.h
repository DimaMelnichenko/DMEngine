#pragma once

#include <optional>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Scatterer\ScattererPass.h"
#include "Light\DMLight.h"
#include "Light\SunPosition.h"
#include "Sky\SkyAtmosphere.h"
#include "PostProcess.h"

// Состав уровня из base.db3: строка таблицы Levels и то, на что она ссылается.
// Пустой optional — у уровня этого нет (колонка NULL)
struct LevelDescription
{
	// Экземпляр модели на уровне (строка LevelModels): одна модель может стоять в нескольких местах
	struct ModelInstance
	{
		uint32_t model = 0;
		XMFLOAT3 position = XMFLOAT3( 0.0f, 0.0f, 0.0f );
		XMFLOAT4 rotation = XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f );	// кватернион x, y, z, w, как rotation узла glTF
		XMFLOAT3 scale = XMFLOAT3( 1.0f, 1.0f, 1.0f );
	};

	// Слой расстановки — растение: модель со всеми её LOD, маска плотности и параметры (таблица ScatterLayers)
	struct ScatterLayer
	{
		uint32_t model = 0;
		std::string mask;
		GS::ScatterPass::PopulateParams params = {};
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
	std::optional<uint32_t> sky;		// модель небесной сферы
	std::vector<ModelInstance> modelInstances;	// таблица LevelModels
	std::vector<ScatterSet> scatterSets;	// таблица LevelScatterSets
	std::optional<Particles> particles;

	// Свет и окружение уровня — как сущности уровня в UE (Directional / Point / Spot Light, Sky Atmosphere,
	// Post Process Volume). Строки настроек неба и постобработки — по ссылке из Levels; NULL — значения по умолчанию
	std::vector<DMLight> lights;				// таблица LevelLights
	std::optional<uint32_t> sunPositionId;		// строка SunPosition: место и время — направление солнца
	std::optional<SunPosition::Settings> sunPosition;
	std::optional<uint32_t> atmosphereId;		// строка SkyAtmosphere
	GS::SkyAtmosphere::Settings atmosphere;
	std::optional<uint32_t> postProcessId;		// строка PostProcessSettings
	GS::PostProcess::Settings postProcess;
};
