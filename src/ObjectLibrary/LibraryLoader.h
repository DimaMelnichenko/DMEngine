#pragma once
#include <memory>
#include <unordered_set>
#include <optional>
#include <string>
#include <vector>
#include "Properties\PropertyContainer.h"
#include "Level\LevelDescription.h"
#include "Level\LevelSaveData.h"

namespace SQLite
{
	class Statement;
	class Database;
}

namespace GS
{
class Material;
class MaterialInstanceStorage;
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
	// Состав уровня name (пустое имя — первый уровень таблицы Levels) с окружением пресета preset (LevelPresets; пусто —
	// строки самого уровня, нет такого — тоже они и строка в логе). Модели уровня не загружает
	bool loadLevel( const std::string& name, const std::string& preset, LevelDescription& level );
	// Пресет окружения на лету: строки пресета (пусто — уровня) — в *Id уровня, их настройки — в level заново
	// (sunPosition, atmosphere, postProcess, wind, heightFog, volumetricCloud); свет и содержимое уровня не трогает
	bool selectLevelPreset( LevelDescription& level, const std::string& preset );
	// «Save preset as»: копии строк окружения активного пресета — новый пресет name уровня (тем же именем — заменяется,
	// кроме заблокированного), правки окон после previous — в его строки (data); пресет становится активным
	bool saveLevelPreset( LevelDescription& level, const std::string& name, const LevelSaveData& data, const LevelSaveData& previous );
	// «Save level» пишет правки, а не окна целиком: в строки — только колонки, которые отличаются от previous (снимок
	// после загрузки или прошлого сохранения, LevelSaveData; RowUpdate). Строки уровня, которой нет (постобработка,
	// ветер, туман, атмосфера вместо незагруженной панорамы), — создаётся, только если в окне что-то поменяли.
	// Свет и окружение одной транзакцией: строки LevelLights — по id (источник с id 0 — новая строка уровня, её id пишется
	// в data.lights; removedLights — удаляются; направление и освещённость, которые считает SunPosition, не пишутся),
	// настройки неба, постобработки, ветра, тумана и облаков — в строки уровня (новая строка — id в level)
	bool saveLevelEnvironment( LevelDescription& level, LevelSaveData& data, const LevelSaveData& previous );
	// Объекты сцены одной транзакцией (LibraryLoaderScene.cpp): строки LevelModels по id (положение, поворот, масштаб;
	// экземпляр с id 0 — новая строка уровня, её id пишется в data.models; removedModels — удаляются), строки Terrain,
	// TerrainErosion, TerrainLayerRules, TerrainLayers и мазки TerrainPaint, ScatterSets, ScatterLayers и
	// ScatterLayerModels по id, WaterSimulation, WaterChannels и Streams уровня, ParticleEmitters по id и место
	// экземпляров LevelParticleEmitters (instanceId)
	bool saveLevelScene( const LevelDescription& level, LevelSaveData& data, const LevelSaveData& previous );
	// Правки параметров экземпляров материалов (окна моделей и слоёв расстановки) — в MaterialParameterInstance одной
	// транзакцией: только изменённые с загрузки или сохранения (Property::modified), строка на параметр — заменяется.
	// Экземпляр — ресурс, а не данные уровня (как ассет Material Instance в UE), но пишется той же «Save level»
	bool saveMaterialInstances( const GS::MaterialInstanceStorage& instances );


	void save();

	// Сгенерированные заново кривые: прежние несправленные строки удаляются, новые (id 0) вставляются и получают id,
	// правленные и ручные не трогаются; отпечаток — в StreamGeneration
	bool saveGeneratedStreams( uint32_t waterSimulationId, const std::string& key, std::vector<GS::StreamCurve>& curves );

private:
	// Загружает текстуры из строк запроса вида "SELECT id, name, file, generate_mipmap, sRGB FROM Textures ..."; count — строк
	// в запросе: тогда каждая текстура — шаг прогресса загрузки (SlowTask)
	bool loadTextures( SQLite::Statement& queryTexture, uint32_t count = 0 );
	void loadScatterLayers( uint32_t idSet, LevelDescription::ScatterSet& set );
	// Строка Terrain уровня и слои его материала (TerrainLayers) — в level.terrain (LibraryLoaderScene.cpp); false — строки нет
	bool loadTerrain( uint32_t terrainId, LevelDescription& level );
	// Правки рельефа террейна уровня (включённые, по порядку слоёв)
	void loadTerrainEdits( uint32_t terrainId, LevelDescription& level );
	// Строка WaterSimulation уровня (level.waterSimulationId); false — строки нет
	bool loadWaterSimulation( LevelDescription& level );
	// Строка WaterChannels строки WaterSimulation (LibraryLoaderScene.cpp); нет таблицы или строки — по умолчанию
	void loadWaterChannels( uint32_t waterSimulationId, GS::WaterChannelsSettings& channels );
	// Кривые русел строки WaterSimulation (Streams, StreamPoints) и отпечаток их генерации (StreamGeneration)
	void loadStreams( uint32_t waterSimulationId, GS::WaterSimulationSettings& water );
	// Строка TerrainErosion, на которую ссылается Terrain.erosion (LibraryLoaderScene.cpp); нет колонки или NULL — эрозии нет
	void loadTerrainErosion( uint32_t terrainId, LevelDescription& level );
	// Строка TerrainLayerRules террейна (LibraryLoaderScene.cpp); нет таблицы, строки или колонки — значения по умолчанию
	void loadTerrainLayerRules( uint32_t terrainId, LevelDescription& level );
	// Строки TerrainPaint террейна — мазки кисти (LibraryLoaderScene.cpp); нет таблицы — мазков нет
	void loadTerrainPaint( uint32_t terrainId, LevelDescription& level );
	// Эмиттеры частиц уровня (LevelParticleEmitters → ParticleEmitters); таблиц нет — эмиттеров нет
	void loadParticleEmitters( LevelDescription& level );
	void loadLevelLights( LevelDescription& level );
	bool loadLevelEnvironment( LevelDescription& level );
	// Пресеты уровня (LibraryLoaderPresets.cpp): имена — в level.presets; строки пресета preset — в *Id (NULL — строка
	// уровня), level.preset и presetLocked; false — пресета нет (остаются строки уровня)
	void loadLevelPresetNames( LevelDescription& level );
	bool usePresetRows( LevelDescription& level, const std::string& preset );
	// Настройки окружения (время суток, небо, постобработка, ветер, туман, облака) — в строки *Id уровня, правки после
	// previous; без строки — новая, привязанная к активному пресету или к уровню
	void saveEnvironmentRows( SQLite::Database& db, LevelDescription& level, const LevelSaveData& data, const LevelSaveData& previous );
	// Звёзды фона неба из таблицы Stars; нет таблицы — без звёзд, строка в логе
	void loadStars( LevelDescription& level );
	// Материал экземпляра (MaterialInstance.id_material); false — экземпляра нет
	bool instanceMaterial( uint32_t idInstance, uint32_t& idMaterial );
	// Имя окна экземпляра материала — последняя часть MaterialInstance.name (без «/»: разделитель пути команды set)
	std::string instanceName( uint32_t idInstance );
	// Число для базы — с короткой записью (%g), как векторы: float 0.1 не превращается в 0.10000000149011612
	static double dbValue( float value );

	std::unordered_set<uint32_t> m_failedMeshes;
};

