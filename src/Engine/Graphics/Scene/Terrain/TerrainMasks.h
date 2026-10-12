#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "TerrainEdits.h"
#include "Level\LevelSettings.h"

namespace GS
{

// Правила слоёв и масок (ступень конвейера рельефа и воды после русел): splat-карта материала террейна и маски плотности
// расстановки и частиц — по итоговому рельефу, как автоматический материал Landscape и PCG по рельефу в UE и маски в
// Gaea / World Machine. Пороги — TerrainLayerRules (строка базы, подокно «Layer rules»). Высоты — доли heightMultiplier
// (0…1, как исходная карта), уклоны — градусы

// Роль слоя материала — колонка TerrainLayers.role; слой без роли ничто не красит, кроме ручных правок рельефа
// (TerrainEdits.paint_layer). Первые роли красят правила слоёв, channel_bed — русла конвейера
enum class TerrainLayerRole : uint32_t
{
	grassDry,	// grass_dry — суше и выше
	talus,		// talus — осыпи, камни, галька промоин на склонах
	rock,		// rock
	snow,		// snow
	grassWet,	// grass_wet — на влажном
	channelBed,	// channel_bed — галька дна и бортов русел и дна озёр: красят русла конвейера и шейдеры по врезу, не правила
	count
};
constexpr uint32_t terrainLayerRoleCount = static_cast<uint32_t>( TerrainLayerRole::count );
// Роли, которые красят правила слоёв (до channel_bed)
constexpr uint32_t terrainRuleRoleCount = static_cast<uint32_t>( TerrainLayerRole::channelBed );
// Имя роли в TerrainLayers.role; не найдено — count
const char* terrainLayerRoleName( TerrainLayerRole role );
TerrainLayerRole terrainLayerRole( const std::string& name );

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
	// Слой материала (TerrainLayers.layer) каждой роли; −1 — слоя с этой ролью нет: его вес — сухой траве (или слою 0)
	std::array<int32_t, terrainLayerRoleCount> layerOf{ -1, -1, -1, -1, -1, -1 };
	// Господствующий ветер — куда дует, по X и Z мира (WindSettings::direction); 0 — без открытости ветру
	float windX = 0.0f;
	float windZ = 0.0f;
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
	std::vector<Mask> masks;				// mask_grass, mask_camomile, mask_pebbles, mask_forest_*, mask_shrubs_*, mask_wind_exposure

	static bool build( const TerrainMaskInputs& inputs, const TerrainLayerRules& rules, TerrainMasks& result );
	// Веса слоёв и масок — к 8 битам (k / 255), как в копии на диске и на GPU: слои и маски из кэша (load) те же, что
	// после расчёта
	void quantize();
	// Копия для просмотра и правки: splatmap.dds (два среза RGBA8, как прежний файл) и маски R8 — в directory
	bool save( const std::string& directory ) const;
	// Та же копия как результат правил — кэш CDLODTerrain::buildMasks: names — маски по порядку; нет файла или не тот
	// размер — false. Слой без веса нигде — пустой, как после расчёта
	bool load( const std::string& directory, uint32_t size, const std::vector<std::string>& names );
	// Отпечаток сборки правил — дата и время компиляции TerrainMasks.cpp: в ключе кэша правка формул меняет его сама
	static const char* codeStamp();
	// Сводка в лог: покрытие слоёв и масок
	std::string summary() const;
};

}
