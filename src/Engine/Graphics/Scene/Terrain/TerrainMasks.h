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
	// Наносы на дне (не правила, а дно под водой — TerrainDetailSplat и terrain.ps): дресва — отмель нижних озёр и дно
	// медленных ручьёв, ил — глубже в нижних озёрах (поля «Water beds» правил слоёв)
	lakeGravel,	// lake_gravel
	lakeSilt,	// lake_silt
	// Подстилка под кронами (не правила, а полог — TerrainCanopy): в шейдере по карте полога забирает вес у трав
	// (terrain_layers.sh, applyForestFloor; сила — «Forest floor» окна террейна)
	forestFloor,	// forest_floor
	// Мох на подстилке: часть веса подстилки, больше на влажном (applyForestFloor; «Forest moss»); пятна — альфой текстуры в
	// высоте слоя (pack_terrain_layer.py --coverage-from-alpha)
	forestMoss,		// forest_moss
	// Подстилка под лиственными (листья): та же доля полога, что у forest_floor, — по доле лиственных крон (карта полога, G)
	forestFloorBroadleaf,	// forest_floor_broadleaf
	count
};
constexpr uint32_t terrainLayerRoleCount = static_cast<uint32_t>( TerrainLayerRole::count );
// Роли, которые красят правила слоёв (до channel_bed)
constexpr uint32_t terrainRuleRoleCount = static_cast<uint32_t>( TerrainLayerRole::channelBed );
// Имя роли в TerrainLayers.role; не найдено — count
const char* terrainLayerRoleName( TerrainLayerRole role );
TerrainLayerRole terrainLayerRole( const std::string& name );

// Слои ролей, когда ни у одной роли слоя нет (−1)
inline std::array<int32_t, terrainLayerRoleCount> noRoleLayers()
{
	std::array<int32_t, terrainLayerRoleCount> layers;
	layers.fill( -1 );
	return layers;
}

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
	std::array<int32_t, terrainLayerRoleCount> layerOf = noRoleLayers();
	// Множитель скорости ветра по рельефу — поле ветра без леса (TerrainWind: поток и подветренная тень); nullptr — без
	// ветра у уровня: снег и лес без него
	const std::vector<float>* windSpeed = nullptr;
};

struct TerrainMasks
{
	static constexpr uint32_t splatLayers = 12;	// TerrainMaterial::maxLayers (static_assert там)
	// Маска открытости ветру (сдувает или гребень, 0…1): её читают флаговые деревья расстановки (ScatterLayers.ridge_lean)
	static constexpr const char* windExposureMask = "mask_wind_exposure";
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
	// Копия для просмотра и правки: splatmap.dds (срезы RGBA8 по четыре слоя) и маски R8 — в directory
	bool save( const std::string& directory ) const;
	// Та же копия как результат правил — кэш TerrainPipeline::buildMasks: names — маски по порядку; нет файла или не тот
	// размер — false. Слой без веса нигде — пустой, как после расчёта
	bool load( const std::string& directory, uint32_t size, const std::vector<std::string>& names );
	// Отпечаток сборки правил — дата и время компиляции TerrainMasks.cpp: в ключе кэша правка формул меняет его сама
	static const char* codeStamp();
	// Сводка в лог: покрытие слоёв и масок
	std::string summary() const;
};

}
