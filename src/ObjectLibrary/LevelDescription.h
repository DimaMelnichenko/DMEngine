#pragma once

#include <optional>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Scatterer\ScattererPass.h"

// Состав уровня из base.db3: строка таблицы Levels и то, на что она ссылается.
// Пустой optional — у уровня этого нет (колонка NULL)
struct LevelDescription
{
	struct Model
	{
		uint32_t id = 0;
		XMFLOAT3 position = XMFLOAT3( 0.0f, 0.0f, 0.0f );
		XMFLOAT3 scale = XMFLOAT3( 1.0f, 1.0f, 1.0f );
	};

	// Слой расстановки: LOD модели, маска плотности и параметры (таблица ScatterLayers)
	struct ScatterLayer
	{
		uint32_t model = 0;
		uint16_t modelLod = 0;
		std::string mask;
		GS::ScatterPass::PopulateParams params = {};
	};

	// Набор расстановки: трава, камешки (таблица ScatterSets)
	struct ScatterSet
	{
		std::string name;
		std::string pass;		// "opaque" или "transparent"
		bool twoSided = false;	// без отсечения задних граней
		std::string colorTexture;
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
	std::vector<Model> models;			// таблица LevelModels
	std::vector<ScatterSet> scatterSets;	// таблица LevelScatterSets
	std::optional<Particles> particles;
};
