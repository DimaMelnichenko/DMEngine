#pragma once

#include "FrameStats.h"

#include <vector>
#include <DirectXCollision.h>
#include "SceneObject.h"
#include "Sky\SkySphere.h"
#include "Sky\SkyAtmosphere.h"
#include "Sky\HDRIBackdrop.h"
#include "Sky\VolumetricCloud.h"
#include "Terrain\CDLODTerrain.h"
#include "Terrain\FarTerrain.h"
#include "Model\ModelInstances.h"
#include "Scatterer\Scatterer.h"
#include "Particle\ParticleSystem.h"
#include "Light\DMLightDriver.h"
#include "Light\ReflectionCaptures.h"
#include "Wind\Wind.h"
#include "Water\WaterSimulation.h"
#include "Foliage\FoliageInteraction.h"
#include "Level\LevelDescription.h"
#include "Level\LevelSaveData.h"

class LibraryLoader;

namespace GS
{

// Содержимое уровня: ресурсы из base.db3, объекты сцены и свет. Состав уровня — строка таблицы Levels
// (террейн, небо, трава, частицы) и его модели из LevelModels. Объекты рисуются в порядке objects() внутри
// своего прохода; объект, которого у уровня нет, остаётся неинициализированным и ничего не делает
class Scene
{
public:
	// Загружает материалы, состав уровня levelName (пустое имя — первый уровень) с окружением пресета preset (пусто —
	// строки уровня), его модели и текстуры
	bool loadResources( LibraryLoader& library, const std::string& levelName, const std::string& preset );
	// Создаёт объекты сцены и свет; вызывается после loadResources() и Renderer::initialize()
	// particles — частицы уровня (-noparticles: нет, кадры с одной точки совпадают)
	bool initialize( LibraryLoader& library, bool particles = true );

	// Раз за кадр до update(): правки источников в GUI и время суток, затем свет солнца у земли — через атмосферу
	// seconds — время кадра: с ним идёт время суток
	void updateLights( float seconds = 0.0f );
	// Константы кадра от сцены по главному виду: масштабы неба и освещения окружением, воздушная перспектива, ветер и
	// вклад объектов (SceneObject::frameConstants: тень облаков, детальная вода, следы в траве, поле ветра). Рендерер
	// добавляет своё (свет, время, туман, затенение окружением) и о самих объектах не знает
	void frameConstants( const RenderView& view, FrameParameters& constants );
	// Масштаб неба и освещения окружением (cb_skyLightScale): освещённость от солнца над атмосферой, лк, или
	// интенсивность панорамы; после compute неба — по показанному результату пересчёта
	float skyLightScale();
	// update() объектов сцены; время CPU каждого — в счётчики кадра («Update: <объект>», stat cpu)
	void update( const FrameContext& frame, FrameStats& stats );
	// Правки редактора, которые требуют тяжёлой перестройки (русла по кривым), — когда правка закончена (ползунок отпущен)
	void applyEditorChanges();
	// Русла заново по кривым (правка в редакторе): террейн, вода, раскладка постоянных слоёв расстановки
	bool rebuildChannels();

	const std::vector<SceneObject*>& objects() const;
	DMLightDriver& lights();
	// Ветер уровня (Levels.wind): константы кадра — Wind::parameters
	Wind& wind();
	const LevelDescription& level() const;
	// Правки GUI — в строки уровня в base.db3 («Save level»): свет, небо, облака, постобработка, туман, ветер, затем
	// террейн, расстановка, вода и частицы. Пишутся только колонки, изменённые после загрузки или прошлого сохранения
	// (снимок — markLevelSaved, LevelSaveData); производное (направление светил по времени суток) — нет
	bool saveLevel( LibraryLoader& library, const PostProcessSettings& postProcess,
					const std::optional<HeightFogSettings>& heightFog );
	// Снимок окон уровня, с которым сравнивает «Save level» — после загрузки, когда окна зарегистрированы в GUI (их
	// Property::markSaved): постобработку и туман отдаёт рендерер
	void markLevelSaved( const PostProcessSettings& postProcess, const std::optional<HeightFogSettings>& heightFog );
	// Пресет окружения на лету (LevelPresets; пусто — строки уровня): его настройки — в окна времени суток, неба, облаков и
	// ветра (постобработку и туман — Renderer::applyEnvironment по level()), правки окружения до него отбрасываются; false —
	// такого пресета нет, ничего не меняется
	bool selectPreset( LibraryLoader& library, const std::string& preset );
	// «Save preset as»: окружение в окнах — новый пресет name (копии строк активного), он становится активным
	bool savePreset( LibraryLoader& library, const std::string& name, const PostProcessSettings& postProcess,
					 const std::optional<HeightFogSettings>& heightFog );
	// После выбора пресета: окружение окон — «сохранённое» (с ним сравнивает «Save level»); остальное — как было
	void markEnvironmentSaved( const PostProcessSettings& postProcess, const std::optional<HeightFogSettings>& heightFog );

	// Отрисовка рельефа (окно террейна, видимость) и его данные — конвейер рельефа и воды: запросы высоты, русла, маски,
	// кисть, поле ветра
	CDLODTerrain& terrain();
	// Горы вокруг карты для тени рельефа (ShadowCascades); без гор heightMap пуст
	FarTerrainHeight farTerrainHeight() const { return m_farTerrain.farHeight(); }
	TerrainPipeline& terrainPipeline();
	// Высоты рельефа для тех, кому нужна только земля (тень рельефа у рендерера): конвейер как TerrainHeightSource
	const TerrainHeightSource& terrainHeightSource() const;
	// Кисть слоёв земли и масок (вьюпорт, команда terrain paint)
	TerrainPaintTool& terrainPaint();
	// Экземпляры моделей уровня (LevelModels)
	ModelInstances& models();
	// Вода по рельефу (Levels.water_simulation); без неё у уровня объект не инициализирован
	WaterSimulation& water();
	// Границы того, что может отбросить тень: террейн (мир × диапазон высот) и модели уровня
	DirectX::BoundingBox bounds() const;
	// Наборы расстановки уровня: трава, камешки
	const std::vector<std::unique_ptr<Scatterer>>& scatterers() const;
	// Частицы уровня; без эмиттеров (или с -noparticles) объект не инициализирован
	ParticleSystem& particles();

private:
	// Небо уровня — процедурная атмосфера (с воздушной перспективой) или HDRI-панорама
	bool hasAtmosphere() const;
	// Масштаб фона неба (cb_skyScale): освещённость от солнца над атмосферой, лк, или интенсивность панорамы
	float skyScale();
	// Множитель объёма воздушной перспективы (cb_aerialPerspectiveScale): cb_skyScale × нормировка объёма
	float aerialPerspectiveScale();
	// Препятствия поля ветра — экземпляры моделей уровня выше 3 м (ели рощи, постройки) кругом по габариту
	std::vector<WindObstacle> windObstacles() const;
	// Окна уровня, собранные в данные для «Save level»
	LevelSaveData levelSaveData( const PostProcessSettings& postProcess, const std::optional<HeightFogSettings>& heightFog );
	// Окна окружения сцены (время суток, небо, облака, ветер) — «сохранённые» (Property::markSaved): после выбора пресета
	void markEnvironmentWindowsSaved();

	LevelDescription m_level;
	LevelSaveData m_savedLevel;		// снимок после загрузки или прошлого «Save level» — с ним сравнивается следующее
	DMLightDriver m_lightDriver;
	Wind m_wind;
	uint64_t m_windRevision = ~0ull;	// номер правки окна «Wind», по которому сверены направление и поле ветра (applyEditorChanges)

	SkyLight m_skyLight;		// освещение окружением: из неба атмосферы или панорамы
	SkyAtmosphere m_atmosphere;	// процедурное небо, от него — освещение окружением и воздушная перспектива
	VolumetricCloud m_clouds;	// облака (Levels.volumetric_cloud), только с атмосферой
	HDRIBackdrop m_hdri;		// панорама вместо атмосферы (Levels.hdri_backdrop)
	bool m_useHDRI = false;
	SkySphere m_sky;			// модель неба уровня (Levels.sky), если задана — вместо фона атмосферы
	TerrainPipeline m_terrainPipeline;	// данные рельефа (конвейер рельефа и воды); его читают террейн, расстановка, вода, частицы
	TerrainPaintTool m_terrainPaint;	// кисть над мазками конвейера
	CDLODTerrain m_terrain;
	FarTerrain m_farTerrain;	// дальние горы вокруг карты (Levels.far_terrain), только с террейном
	WaterSimulation m_water;	// вода по рельефу (Levels.water_simulation), только с террейном
	ModelInstances m_models;
	std::vector<std::unique_ptr<Scatterer>> m_scatterers;
	ParticleSystem m_particles;	// частицы уровня (LevelParticleEmitters); -noparticles — нет
	FoliageInteraction m_foliageInteraction;	// трава расступается у камеры (только с террейном)
	ReflectionCaptures m_reflectionCaptures;	// карты отражений места (LevelReflectionCaptures)

	std::vector<SceneObject*> m_objects;
};

}
