#pragma once

#include <chrono>
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
#include "Sound\AmbientSound.h"
#include "Sound\Soundscape.h"
#include "Sound\SoundAreaOverlay.h"
#include "Sound\SoundDebug.h"
#include "World\WorldEntities.h"
#include "Model\ModelCollision.h"
#include "Engine\Simulation\World.h"
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
	// particles — частицы уровня (-noparticles: нет, кадры с одной точки совпадают); audio — микшер звука движка: голоса
	// звуков окружения (nullptr — уровень без звука)
	bool initialize( LibraryLoader& library, bool particles = true, AudioMixer* audio = nullptr );

	// Раз за кадр, первым: правки времени в окне «Sun position» — в мировые часы, шаг часов (seconds — реальное время
	// кадра) и шаги систем мира симуляции; число шагов мира в этом кадре
	uint32_t advanceWorld( double seconds );
	// Раз за кадр после advanceWorld, до update(): время часов — в окно, правки источников в GUI и время суток — в свет,
	// затем свет солнца у земли — через атмосферу
	void updateLights();
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
	// Мир симуляции: сущности, системы, мировые часы (время суток, время игры)
	World& world() { return m_world; }
	// Сущности мира на экране и окно «World»
	WorldEntities& worldEntities() { return m_worldEntities; }
	// Звуки окружения и певцы: окна раздела Audio, метки во вьюпорте
	AmbientSound& ambientSound() { return m_ambientSound; }
	Soundscape& soundscape() { return m_soundscape; }
	// Режим вьюпорта «Sound sources» (флажок «Show sources» окна «Audio», ставит Engine каждый кадр): метки звука во вьюпорте,
	// маски мест областей с «Show area» — подсветкой земли (update)
	void setShowSoundSources( bool show ) { m_showSoundSources = show; }
	bool showSoundSources() const { return m_showSoundSources; }
	// Метки звука кадра (значки источников, области) — вьюпорту
	SoundDebug soundDebug();
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
	// Мир — до тех, кто держит ссылку на его часы (время суток у SunPosition, певцы): уничтожается после них
	World m_world;
	// Формы моделей в мире столкновений — раз на модель: экземпляры уровня, лес и камни расстановки
	ModelCollision m_modelCollision{ m_world.physics() };
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
	// Карта полога собрана по этим кронам: сумма номеров крон наборов (Scatterer::canopyRevision) и правок моделей уровня
	uint64_t m_canopyKey = ~0ull;
	// Препятствия воде — камни русел расстановки и камни-модели уровня: номера правки, по которым они отданы воде, и когда
	// номер сменился в последний раз (правку экземпляров отдаём, когда она устоялась)
	uint64_t m_streamStoneKey = 0;
	std::chrono::steady_clock::time_point m_streamStoneChanged;
	bool m_streamStonePending = false;
	ParticleSystem m_particles;	// частицы уровня (LevelParticleEmitters); -noparticles — нет
	FoliageInteraction m_foliageInteraction;	// трава расступается у камеры (только с террейном)
	ReflectionCaptures m_reflectionCaptures;	// карты отражений места (LevelReflectionCaptures)
	AmbientSound m_ambientSound;	// звуки окружения (Levels.ambient_sound): голоса микшера звука
	Soundscape m_soundscape;		// певцы (SoundscapeLayers): фразы записей вокруг слушателя
	WorldEntities m_worldEntities;	// сущности мира симуляции на экране
	bool m_showSoundSources = false;
	SoundAreaOverlay m_soundAreas;	// маски мест звука одной текстурой — подсветка земли

	std::vector<SceneObject*> m_objects;
};

}
