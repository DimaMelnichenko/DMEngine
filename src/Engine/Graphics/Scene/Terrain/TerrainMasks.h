#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "TerrainEdits.h"

namespace GS
{

// Правила слоёв и масок (ступень конвейера рельефа и воды после русел): splat-карта материала террейна и маски плотности
// расстановки и частиц — по итоговому рельефу, как автоматический материал Landscape и PCG по рельефу в UE и маски в
// Gaea / World Machine. Пороги — значения прежнего Tools/gen_terrain_textures.py; строка базы и подокно GUI — следующим шагом
// (TODO.md, этап 8). Высоты — доли heightMultiplier (0…1, как исходная карта), уклоны — градусы
struct TerrainMaskSettings
{
	struct Range
	{
		float from;
		float to;
	};
	// Слои материала (TerrainLayers.layer), которые красят правила; галька русел (5) — покраска правками
	struct Layers
	{
		uint32_t grassDry = 0;	// patchy_meadow1 — суше и выше
		uint32_t talus = 1;		// осыпи, камни, галька промоин на склонах
		uint32_t rock = 2;
		uint32_t snow = 3;
		uint32_t grassWet = 4;	// wispy_grass_meadow — на влажном
	} layers;

	Range rockSlope{ 27.0f, 37.0f };		// скала — крутые склоны (± шум 10°)
	Range bedrockWear{ 1.0f, 3.0f };		// и коренная порода в промоинах (размыв эрозии) на средних уклонах
	Range bedrockSlope{ 14.0f, 24.0f };
	Range snowHeight{ 0.80f, 0.88f };		// снег — по высоте, на северных склонах ниже на snowNorth
	float snowNorth = 0.06f;
	Range talusMap{ 0.15f, 0.8f };			// осыпь эрозии у подножий скал
	Range boulderSlope{ 18.0f, 25.0f };		// и камни на крутоватых склонах
	Range gullyFlow{ 3.3f, 4.0f };			// галька промоин: log10 водосбора, м², на склонах gullySlope
	Range gullySlope{ 6.0f, 12.0f };
	Range moistureTwi{ 4.0f, 8.0f };		// влажность — TWI = ln( a / tan β )
	Range treeline{ 0.72f, 0.79f };			// граница леса (на северных склонах ниже)
	Range forestSlope{ 30.0f, 38.0f };		// круче — леса нет
	Range valleyFloorHeight{ 0.32f, 0.22f };// дно долины — луга с редкими рощами
	Range valleyFloorSlope{ 9.0f, 4.0f };
};

// Входы правил: итоговый рельеф (после эрозии, ручных правок и русел) и что о нём известно. Поля — строки подряд,
// size × size; пустое поле — правило без него (карты эрозии — нули, русла — по водосбору)
struct TerrainMaskInputs
{
	HeightField field;
	const std::vector<float>* wear = nullptr;		// карты эрозии (eroded\): размыв, отложения, осыпь
	const std::vector<float>* deposition = nullptr;
	const std::vector<float>* talus = nullptr;
	const std::vector<float>* catchment = nullptr;	// водосбор итогового рельефа, м² (TerrainHydrology::catchment)
	const std::vector<float>* water = nullptr;		// 1 — русло или озеро конвейера (TerrainHydrology), 0 — суша
};

struct TerrainMasks
{
	static constexpr uint32_t splatLayers = 8;
	struct Mask
	{
		std::string name;		// имя текстуры: его указывают ScatterLayers.mask и ParticleEmitters.mask
		std::vector<float> values;
	};

	uint32_t size = 0;
	std::vector<float> splat[splatLayers];	// веса слоёв 0…1, сумма — 1; пустой — слой не красится
	std::vector<Mask> masks;				// mask_grass, mask_camomile, mask_pebbles, mask_forest_*, mask_shrubs_*

	static bool build( const TerrainMaskInputs& inputs, const TerrainMaskSettings& settings, TerrainMasks& result );
	// Копия для просмотра и правки: splatmap.dds (два среза RGBA8, как прежний файл) и маски R8 — в directory
	bool save( const std::string& directory ) const;
	// Сводка в лог: покрытие слоёв и масок
	std::string summary() const;
};

}
