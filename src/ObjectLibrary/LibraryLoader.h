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
							   const GS::PostProcessSettings& postProcess, const GS::WindSettings& wind,
							   const std::optional<GS::HeightFogSettings>& heightFog,
							   const std::optional<GS::VolumetricCloudSettings>& cloud );
	// Правки объектов сцены одной транзакцией (LibraryLoaderScene.cpp): строки LevelModels по id (положение, поворот,
	// масштаб), строка Terrain и tiling TerrainLayers, строки ScatterLayers и ScatterLayerModels по id, строка
	// WaterSimulation уровня, строки ParticleEmitters по id
	bool saveLevelScene( const LevelDescription& level, const std::vector<LevelDescription::ModelInstance>& models,
						 const std::optional<GS::TerrainSettings>& terrain,
						 const std::vector<GS::ScatterLayerRecord>& scatterLayers,
						 const std::optional<GS::WaterSimulationSettings>& water,
						 const std::vector<GS::ParticleEmitterSettings>& particleEmitters );


	void save();

private:
	// Загружает текстуры из строк запроса вида "SELECT id, name, file, generate_mipmap, sRGB FROM Textures ..."
	bool loadTextures( SQLite::Statement& queryTexture );
	void loadScatterLayers( uint32_t idSet, LevelDescription::ScatterSet& set );
	// Правки рельефа террейна уровня (включённые, по порядку слоёв)
	void loadTerrainEdits( uint32_t terrainId, LevelDescription& level );
	// Строка WaterSimulation уровня (level.waterSimulationId); false — строки нет
	bool loadWaterSimulation( LevelDescription& level );
	// Строка WaterChannels строки WaterSimulation (LibraryLoaderScene.cpp); нет таблицы или строки — по умолчанию
	void loadWaterChannels( uint32_t waterSimulationId, GS::WaterChannelsSettings& channels );
	// Строка TerrainErosion, на которую ссылается Terrain.erosion (LibraryLoaderScene.cpp); нет колонки или NULL — эрозии нет
	void loadTerrainErosion( uint32_t terrainId, LevelDescription& level );
	// Эмиттеры частиц уровня (LevelParticleEmitters → ParticleEmitters); таблиц нет — эмиттеров нет
	void loadParticleEmitters( LevelDescription& level );
	void loadLevelLights( LevelDescription& level );
	bool loadLevelEnvironment( LevelDescription& level );
	// Материал экземпляра (MaterialInstance.id_material); false — экземпляра нет
	bool instanceMaterial( uint32_t idInstance, uint32_t& idMaterial );
	// Число для базы — с короткой записью (%g), как векторы: float 0.1 не превращается в 0.10000000149011612
	static double dbValue( float value );

	std::unordered_set<uint32_t> m_failedMeshes;
};

