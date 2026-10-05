#pragma once
#include <memory>
#include <unordered_set>
#include <optional>
#include <string>
#include <vector>
#include "Properties\PropertyContainer.h"
#include "Level\LevelDescription.h"

namespace SQLite
{
	class Statement;
}

namespace GS
{
class Material;
}

class LibraryLoader
{
public:
	LibraryLoader();
	~LibraryLoader();

	bool loadAllTextures();
	bool loadTexture( uint32_t idTexture );
	bool loadAllMaterials();
	bool loadMaterial( uint32_t idMaterial );
	bool loadMesh( uint32_t idMesh );
	bool loadModelWithLOD( uint32_t idModel );
	// Значения экземпляра материала (MaterialParameterInstance) поверх определений paramSet
	bool loadMaterialParams( uint32_t idInstance, PropertyContainer& paramSet );
	bool loadMaterialParamDef( uint32_t idMaterial, PropertyContainer& paramSet );
	bool loadShader( uint32_t idMaterial, GS::Material* material );
	// Состав уровня name (пустое имя — первый уровень таблицы Levels). Модели уровня не загружает
	bool loadLevel( const std::string& name, LevelDescription& level );
	// Свет и окружение уровня обратно в базу одной транзакцией: строки LevelLights — по id, настройки неба
	// и постобработки — в строки уровня (нет строки — создаётся и привязывается к Levels, id пишется в level)
	bool saveLevelEnvironment( LevelDescription& level, const std::vector<DMLight>& lights,
							   const std::optional<GS::SunPositionSettings>& sunPosition,
							   const std::optional<GS::SkyAtmosphereSettings>& atmosphere,
							   const std::optional<GS::HDRIBackdropSettings>& hdri,
							   const GS::PostProcessSettings& postProcess, const GS::WindSettings& wind );


	void save();

private:
	// Загружает текстуры из строк запроса вида "SELECT id, name, file, generate_mipmap, sRGB FROM Textures ..."
	bool loadTextures( SQLite::Statement& queryTexture );
	void loadScatterLayers( uint32_t idSet, LevelDescription::ScatterSet& set );
	// Правки рельефа террейна уровня (включённые, по порядку слоёв)
	void loadTerrainEdits( uint32_t terrainId, LevelDescription& level );
	// Строка WaterSimulation уровня (level.waterSimulationId); false — строки нет
	bool loadWaterSimulation( LevelDescription& level );
	void loadLevelLights( LevelDescription& level );
	bool loadLevelEnvironment( LevelDescription& level );
	// Материал экземпляра (MaterialInstance.id_material); false — экземпляра нет
	bool instanceMaterial( uint32_t idInstance, uint32_t& idMaterial );

	std::unordered_set<uint32_t> m_failedMeshes;
};

