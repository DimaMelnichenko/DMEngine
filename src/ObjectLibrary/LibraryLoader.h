#pragma once
#include <memory>
#include <unordered_set>
#include "Shaders\DMShader.h"
#include "LevelDescription.h"

namespace SQLite
{
	class Statement;
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
	bool loadShader( uint32_t idMaterial, GS::DMShader* );
	// Состав уровня name (пустое имя — первый уровень таблицы Levels). Модели уровня не загружает
	bool loadLevel( const std::string& name, LevelDescription& level );
	// Свет и окружение уровня обратно в базу одной транзакцией: строки LevelLights — по id, настройки неба
	// и постобработки — в строки уровня (нет строки — создаётся и привязывается к Levels, id пишется в level)
	bool saveLevelEnvironment( LevelDescription& level, const std::vector<DMLight>& lights,
							   const std::optional<SunPosition::Settings>& sunPosition,
							   const std::optional<GS::SkyAtmosphere::Settings>& atmosphere,
							   const std::optional<GS::HDRIBackdrop::Settings>& hdri,
							   const GS::PostProcess::Settings& postProcess );


	void save();

private:
	// Загружает текстуры из строк запроса вида "SELECT id, name, file, generate_mipmap, sRGB FROM Textures ..."
	bool loadTextures( SQLite::Statement& queryTexture );
	void loadScatterLayers( uint32_t idSet, LevelDescription::ScatterSet& set );
	void loadLevelLights( LevelDescription& level );
	bool loadLevelEnvironment( LevelDescription& level );
	// Материал экземпляра (MaterialInstance.id_material); false — экземпляра нет
	bool instanceMaterial( uint32_t idInstance, uint32_t& idMaterial );

	std::unordered_set<uint32_t> m_failedMeshes;
};

