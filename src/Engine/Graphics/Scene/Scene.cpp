#include "Scene.h"
#include "Engine\Simulation\WanderSystem.h"
#include "Engine\Simulation\MovementSystem.h"
#include "Engine\Animation\AnimationSystem.h"
#include "ObjectLibrary\LibraryLoader.h"
#include <algorithm>
#include <chrono>
#include "Assets.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "ConstantBuffers.h"
#include "Shaders\atmosphere_constants.h"

using namespace DirectX;

namespace
{

std::string elapsedMs( std::chrono::high_resolution_clock::time_point start )
{
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - start );
	return std::to_string( elapsed.count() / 1000.0 );
}

// Окружение снимка «Save level» — то, что входит в пресет (время суток, небо, постобработка, ветер, туман, облака)
void copyEnvironment( const LevelSaveData& from, LevelSaveData& to )
{
	to.sunPosition = from.sunPosition;
	to.atmosphere = from.atmosphere;
	to.hdri = from.hdri;
	to.postProcess = from.postProcess;
	to.wind = from.wind;
	to.heightFog = from.heightFog;
	to.cloud = from.cloud;
}

}

namespace GS
{

bool Scene::loadResources( LibraryLoader& library, const std::string& levelName, const std::string& preset )
{
	// Доли — по времени загрузки: текстуры (PNG моделей с мипами на CPU) — почти всё
	SlowTask task( 1.0f );
	task.enterProgressFrame( 0.02f, "Materials" );
	LOG( "Load materials" );
	auto timeStart = std::chrono::high_resolution_clock::now();
	if( !library.loadAllMaterials() )
		return false;
	LOG( "Load material for ms: " + elapsedMs( timeStart ) );

	if( !library.loadLevel( levelName, preset, m_level ) )
		return false;
	LOG( "Level: " + m_level.name );

	task.enterProgressFrame( 0.03f, "Models" );
	LOG( "Load models" );
	timeStart = std::chrono::high_resolution_clock::now();
	for( const LevelDescription::ModelInstance& instance : m_level.modelInstances )
	{
		if( !library.loadModelWithLOD( instance.model ) )
			return false;
	}

	// Небесную сферу и модели расстановки рисуют свои объекты сцены, но загружаются они вместе с моделями уровня
	if( m_level.sky && !library.loadModelWithLOD( *m_level.sky ) )
		return false;

	for( const LevelDescription::ScatterSet& set : m_level.scatterSets )
	{
		for( const LevelDescription::ScatterLayer& layer : set.layers )
		{
			for( const LevelDescription::ScatterModel& model : layer.models )
			{
				if( !library.loadModelWithLOD( model.model ) )
					return false;
			}
		}
	}
	// Скелеты и клипы моделей со скелетом — в библиотеку анимации мира
	library.loadAnimations( m_world.animations() );
	LOG( "Load models for ms: " + elapsedMs( timeStart ) );

	task.enterProgressFrame( 0.95f, "Textures" );
	LOG( "Load textures" );
	timeStart = std::chrono::high_resolution_clock::now();
	try
	{
		if( !library.loadAllTextures() )
			return false;
	}
	catch( const std::exception& e )
	{
		LOG( e.what() );
		return false;
	}
	LOG( "Load textures for ms: " + elapsedMs( timeStart ) );

	return true;
}

bool Scene::initialize( LibraryLoader& library, bool particles, AudioMixer* audio )
{
	// Доли — по времени (Release, уровень Test): террейн с эрозией, руслами и слоями материала ~2 с, течение озёр ~0,6 с
	SlowTask task( 2.8f );
	task.enterProgressFrame( 0.1f, "Sky" );
	LOG( "Create light driver" );
	if( !m_lightDriver.Initialize() )
		return false;
	if( m_level.lights.empty() )
		LOG( "Level has no lights in table LevelLights, default light is used" );
	m_lightDriver.load( m_level.lights, m_world.clock(), m_level.sunPosition );
	m_lightDriver.update();

	if( !m_skyLight.initialize() )
	{
		LOG( "Fail to initialize sky light" );
		return false;
	}

	// Небо уровня — панорама или процедурная атмосфера; файл панорамы не загрузился — атмосфера, как заглушка.
	// Небо освещает сцену всегда, а фоном рисуется, если у уровня нет своей модели неба
	if( m_level.hdriBackdrop )
	{
		m_useHDRI = m_hdri.initialize( *m_level.hdriBackdrop, m_skyLight );
		if( !m_useHDRI )
			LOG( "HDRI backdrop is not loaded, sky atmosphere is used instead" );
	}
	if( m_useHDRI )
		m_hdri.setBackgroundVisible( !m_level.sky );
	else
	{
		if( !m_atmosphere.initialize( m_lightDriver, m_level.atmosphere, m_level.stars, m_skyLight ) )
		{
			LOG( "Fail to initialize sky atmosphere" );
			return false;
		}
		m_atmosphere.setBackgroundVisible( !m_level.sky );
	}
	m_wind.initialize( m_level.wind );
	updateLights();
	if( m_level.volumetricCloud )
	{
		if( m_useHDRI )
		{
			LOG( "Volumetric cloud needs the sky atmosphere, skipped" );
		}
		else if( !m_clouds.initialize( *m_level.volumetricCloud, m_atmosphere, m_wind ) )
		{
			LOG( "Fail to initialize volumetric cloud" );
			return false;
		}
	}

	// Расстановка, вода и частицы стоят на террейне и читают его карту высот (TerrainHeightSource)
	auto timeStart = std::chrono::high_resolution_clock::now();
	task.enterProgressFrame( 2.0f, "Terrain" );
	if( m_level.terrain )
	{
		// Рельеф с ручными правками, затем русла и ручьи по нему (конвейер рельефа и воды) — если у уровня есть вода. Ветер
		// уровня — господствующий: от него открытость ветру в правилах слоёв (снег, граница леса); без ветра — без неё.
		// Направление — из окна «Wind» (угол Yaw), как потом его сравнивает applyEditorChanges
		m_terrainPipeline.setPrevailingWind( m_level.wind ? m_wind.settings().direction : XMFLOAT3( 0.0f, 0.0f, 0.0f ) );
		m_terrainPipeline.setWindField( m_wind.settings().field );
		m_terrainPipeline.setPaint( m_level.terrainPaint );
		if( !m_terrainPipeline.initialize( *m_level.terrain, m_level.terrainEdits,
										   m_level.waterSimulation ? &*m_level.waterSimulation : nullptr,
										   m_level.terrainErosion ? &*m_level.terrainErosion : nullptr, m_level.terrainLayerRules ) )
			return false;
		m_terrainPaint.initialize( m_terrainPipeline );
		if( !m_terrain.initialize( m_level.terrain->settings, m_terrainPipeline, m_terrainPaint ) )
			return false;
		// Дальние горы вокруг карты — на её сетке узлов; не вышло — уровень без них
		if( m_level.farTerrain && !m_farTerrain.initialize( *m_level.farTerrain, m_terrain, m_terrainPipeline ) )
			LOG( "Far terrain is not initialized" );
		LOG( "Terrain init ms: " + elapsedMs( timeStart ) );
	}
	const bool hasWater = m_level.terrain && m_level.waterSimulation;
	if( m_level.farTerrain && !m_level.terrain )
		LOG( "Far terrain needs a terrain, skipped" );
	if( m_level.waterSimulation && !m_level.terrain )
		LOG( "Water simulation needs a terrain, skipped" );
	// Кривые русел сгенерированы заново — в базу (это данные: их правят окно «Streams» и Tools/stream_edit.py)
	if( hasWater && m_terrainPipeline.streamsRegenerated() && m_level.waterSimulationId )
	{
		m_level.waterSimulation->streams = m_terrainPipeline.streamCurves();
		m_level.waterSimulation->streamsKey = m_terrainPipeline.streamsKey();
		if( !library.saveGeneratedStreams( *m_level.waterSimulationId, m_level.waterSimulation->streamsKey, m_level.waterSimulation->streams ) )
			LOG( "Stream curves are not saved to the database" );
	}
	task.enterProgressFrame( 0.6f, "Water" );
	if( hasWater && ( !m_terrainPipeline.hydrology() ||
					  !m_water.initialize( *m_level.waterSimulation, m_terrainPipeline, *m_terrainPipeline.hydrology() ) ) )
		return false;
	task.enterProgressFrame( 0.1f, "Models, scatter and particles" );

	if( m_level.sky )
		m_sky.setModel( *m_level.sky );
	m_models.initialize( m_level.modelInstances, &m_world.animations() );
	// Эрозия посчитана заново — экземпляры моделей на новую землю (правка окна «Model instances», в базу — «Save level»)
	if( m_terrainPipeline.erosionChanged() )
	{
		const size_t moved = m_models.reseat( [this]( float x, float z, float radius ) { return m_terrainPipeline.erosionShift( x, z, radius ); } );
		LOG( "Model instances moved to the new eroded terrain: " + std::to_string( moved ) + " (File > Save level keeps it)" );
		m_terrainPipeline.releaseErosionChange();
	}
	// Поле ветра по рельефу — после масок леса и моделей уровня: ели рощи и постройки (выше 3 м) — препятствия
	if( m_level.terrain && m_level.wind && !m_terrainPipeline.buildWindField( windObstacles() ) )
		LOG( "Wind field is not built, the wind of the level is even" );

	timeStart = std::chrono::high_resolution_clock::now();
	for( const LevelDescription::ScatterSet& set : m_level.scatterSets )
	{
		if( !m_level.terrain )
		{
			LOG( "Scatter set " + set.name + " needs a terrain, skipped" );
			continue;
		}

		auto scatterer = std::make_unique<Scatterer>( set.name, set.settings );
		if( !scatterer->Initialize() )
			return false;
		scatterer->setTerrain( &m_terrainPipeline );
		const auto& layersOfRoles = m_terrainPipeline.layersOfRoles();
		scatterer->setGroundLayers( std::vector<int32_t>( layersOfRoles.begin(), layersOfRoles.end() ) );

		for( const LevelDescription::ScatterLayer& layer : set.layers )
		{
			std::vector<Scatterer::LayerModel> models;
			for( const LevelDescription::ScatterModel& variant : layer.models )
			{
				DMModel* model = Assets::models().get( variant.model ).get();
				if( !model || model->lodCount() == 0 )
				{
					LOG( "Scatter set " + set.name + ": model " + std::to_string( variant.model ) + " has no LOD" );
					return false;
				}
				models.push_back( { model, variant.weight, variant.castShadow, variant.id, variant.lodDistances, variant.placement } );
			}
			if( models.empty() )
			{
				LOG( "Scatter set " + set.name + ": layer without models (ScatterLayerModels), skipped" );
				continue;
			}
			if( !scatterer->addLayer( models, layer.mask, layer.settings ) )
				return false;
		}
		m_scatterers.push_back( std::move( scatterer ) );
	}
	// Камни русел — часть конвейера рельефа и воды: их ставят слои stream_stones
	if( m_terrainPipeline.hydrology() )
		for( const auto& scatterer : m_scatterers )
			scatterer->setStreamStones( m_terrainPipeline.hydrology()->stones );
	LOG( "Scatter init ms: " + elapsedMs( timeStart ) );

	if( particles && !m_level.particleEmitters.empty() &&
		!m_particles.initialize( m_level.particleEmitters, m_level.terrain ? &m_terrainPipeline : nullptr ) )
	{
		LOG( "Fail to initialize particles" );
		return false;
	}

	// Карты отражений места; не вышло — вода отражает небо, где промахнулись экранные отражения
	if( !m_level.reflectionCaptures.empty() && !m_reflectionCaptures.initialize( m_level.reflectionCaptures ) )
		LOG( "Reflection captures are not initialized" );

	// Звуки окружения — голоса микшера по ветру и маскам травы и леса; без террейна — только ветер
	if( audio && m_level.ambientSound &&
		!m_ambientSound.initialize( *m_level.ambientSound, *audio, m_wind, m_level.terrain ? &m_terrainPipeline : nullptr ) )
		LOG( "Ambient sound is not initialized" );
	// Певцы — записи (Sounds\) по слоям уровня, места — по маскам, часы — по времени суток, правило «Wind» — по ветру уровня.
	// Окно — и у уровня без слоёв: слой добавляют в нём
	if( audio &&
		!m_soundscape.initialize( m_level.soundscapeLayers, m_level.sounds, *audio, m_level.terrain ? &m_terrainPipeline : nullptr,
								  m_world.clock(), m_wind ) )
		LOG( "Soundscape is not initialized" );

	// Мир симуляции: земля — рельеф уровня; столкновения — земля и тела моделей уровня; системы — по порядку шага:
	// поведение решает, куда идти, передвижение двигает
	m_world.setTerrain( m_level.terrain ? &m_terrainPipeline : nullptr );
	timeStart = std::chrono::high_resolution_clock::now();
	if( m_level.terrain && !m_world.physics().buildTerrain( m_terrainPipeline ) )
		LOG( "Physics terrain is not built: walking and creatures go through the ground" );
	m_models.setCollision( &m_modelCollision );
	m_world.physics().optimize();
	LOG( "Physics: terrain and " + std::to_string( m_world.physics().bodyCount() ) + " bodies of model instances, ms: " + elapsedMs( timeStart ) );
	// Постоянные слои расстановки (лес, камни русел) — тела после своей раскладки (лес — по копии пула с GPU)
	for( const auto& scatterer : m_scatterers )
		scatterer->setCollision( &m_modelCollision );
	m_world.addSystem( std::make_unique<WanderSystem>() );
	m_world.addSystem( std::make_unique<MovementSystem>() );
	// Анимация — после передвижения: шаг по фактической скорости
	m_world.addSystem( std::make_unique<AnimationSystem>() );
	m_worldEntities.initialize( m_world );

	// Следы в траве — по земле под камерой; без поля растения просто не расступаются
	if( m_level.terrain && !m_foliageInteraction.initialize( &m_terrainPipeline ) )
		LOG( "Foliage interaction is not initialized" );

	// Небо первым: его compute() готовит освещение окружением для всех, render() рисует фон
	SceneObject* sky = m_useHDRI ? static_cast<SceneObject*>( &m_hdri ) : &m_atmosphere;
	// Конвейер рельефа — до тех, кто читает его ресурсы сцены (рельеф, расстановка, частицы): его compute() их привязывает
	m_objects = { sky, &m_sky, &m_terrainPipeline, &m_terrain };
	// Горы — после рельефа карты: его update() считает общую таблицу дальностей, их — берёт её
	if( m_farTerrain.initialized() )
		m_objects.push_back( &m_farTerrain );
	m_objects.push_back( &m_models );
	// Сущности мира — модели, как экземпляры уровня
	m_objects.push_back( &m_worldEntities );
	if( m_foliageInteraction.initialized() )
		m_objects.push_back( &m_foliageInteraction );
	// Облака — сразу после неба: их compute() читает таблицы атмосферы и освещение окружением, фон рисуют поверх неба
	if( m_clouds.initialized() )
		m_objects.insert( m_objects.begin() + 1, &m_clouds );
	// Вода — до тех, кто её читает: её compute() привязывает текстуру воды в слот сцены (террейн рисует её позже)
	if( hasWater )
		m_objects.push_back( &m_water );
	for( const auto& scatterer : m_scatterers )
		m_objects.push_back( scatterer.get() );
	// Частицы — после воды и расстановки: читают воду (SLOT_WATER) в compute
	if( m_particles.initialized() )
		m_objects.push_back( &m_particles );
	// Карты отражений места — своих мешей нет: просят съёмку сцены, префильтр и слот сцены — в compute
	if( m_reflectionCaptures.initialized() )
		m_objects.push_back( &m_reflectionCaptures );
	// Звуки окружения — мешей нет: цели голосов микшера по камере
	if( m_ambientSound.initialized() )
		m_objects.push_back( &m_ambientSound );
	if( m_soundscape.initialized() )
		m_objects.push_back( &m_soundscape );

	return true;
}

uint32_t Scene::advanceWorld( double seconds )
{
	m_lightDriver.takeClockEdits();
	return m_world.tick( seconds );
}

void Scene::updateLights()
{
	m_lightDriver.update();
	// Atmosphere Sun Light, как в UE: солнце задано над атмосферой, у земли его свет — прошедший атмосферу
	XMFLOAT3 toSun;
	XMFLOAT3 color;
	m_lightDriver.directionalLight( toSun, color );
	// У панорамы атмосферы нет: свет солнца у земли такой, как задан. Луна — второе светило атмосферы, то же пропускание
	m_lightDriver.setAtmosphereTransmittance( 0, m_useHDRI ? XMFLOAT3( 1.0f, 1.0f, 1.0f ) : m_atmosphere.sunTransmittance( toSun ) );
	XMFLOAT3 toMoon;
	if( m_lightDriver.moonLight( toMoon, color ) )
		m_lightDriver.setAtmosphereTransmittance( 1, m_useHDRI ? XMFLOAT3( 1.0f, 1.0f, 1.0f ) : m_atmosphere.sunTransmittance( toMoon ) );
}

bool Scene::hasAtmosphere() const
{
	return !m_useHDRI;
}

float Scene::aerialPerspectiveScale()
{
	return m_useHDRI ? 0.0f : skyScale() * m_atmosphere.skyNormalization();
}

float Scene::skyScale()
{
	return m_useHDRI ? m_hdri.intensity() : m_lightDriver.sunIlluminance();
}

float Scene::skyLightScale()
{
	// Результат SkyLight хранит свет, делённый на нормировку своего пересчёта
	return skyScale() * m_skyLight.normalization();
}

void Scene::applyEditorChanges()
{
	// Правили кривые русел или форму русел в редакторе — русла, вода и расстановка заново, без перезагрузки уровня
	if( m_level.terrain && m_level.waterSimulation && m_water.rebuildRequested() )
		rebuildChannels();
	// Правили течение статичной воды (подокно «Flow») — течение озёр заново
	if( m_level.waterSimulation )
	{
		m_water.applyFlowChanges();
		// Правили настройки пены (подокно «Surface») — поле пены заново
		m_water.applyFoamChanges();
	}
	// Правили правила слоёв (подокно «Layer rules» террейна) или повернули ветер (окно «Wind», когда правка закончилась) —
	// слои и маски заново; постоянные слои (лес) разложены по прежним маскам — раскладка заново, кольцевые читают маски
	// каждый кадр
	bool windTurned = false, windTerrainChanged = false;
	// Окно «Wind» не правили с прошлой сверки — сверять нечего
	if( m_level.terrain && m_level.wind && !m_wind.properties()->editing() && m_wind.properties()->revision() != m_windRevision )
	{
		m_windRevision = m_wind.properties()->revision();
		const Wind::Settings wind = m_wind.settings();
		windTurned = m_terrainPipeline.prevailingWindChanged( wind.direction );
		if( windTurned )
			m_terrainPipeline.setPrevailingWind( wind.direction );
		// Правили подокно «Field» — поле ветра заново; поворот ветра и настройки рельефной части (по ней снег) пересчитают его
		// вместе с масками ниже
		if( m_terrainPipeline.windFieldChanged( wind.field ) )
		{
			windTerrainChanged = m_terrainPipeline.windTerrainChanged( wind.field );
			m_terrainPipeline.setWindField( wind.field );
			if( !windTurned && !windTerrainChanged && !m_terrainPipeline.rebuildWindField() )
				LOG( "Wind field is not rebuilt" );
		}
	}
	// Правили материал растения (подокно «Materials» слоя, окно модели) — импостеры этих моделей запекаются заново
	for( const auto& scatterer : m_scatterers )
		scatterer->rebakeChangedImpostors();
	// Мазок кисти изменил маски — постоянные слои (лес) раскладываются заново
	if( m_level.terrain && m_terrainPipeline.takePaintedMasks() )
		for( const auto& scatterer : m_scatterers )
			scatterer->terrainChanged();
	if( m_level.terrain && ( windTurned || windTerrainChanged || m_terrainPipeline.layerRulesChanged() ) )
	{
		if( m_terrainPipeline.rebuildMasks() )
			for( const auto& scatterer : m_scatterers )
				scatterer->terrainChanged();
		else
			LOG( "Terrain layers and masks are not rebuilt" );
	}
}

void Scene::update( const FrameContext& frame, FrameStats& stats )
{
	for( SceneObject* object : m_objects )
	{
		const auto start = std::chrono::high_resolution_clock::now();
		object->update( frame );
		stats.add( "Update: " + object->name() + " = %.3f ms",
				   std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - start ).count() / 1000.0f );
	}

	// Места звука на земле (режим «Sound sources», «Show area»): маски областей — подсветкой террейна, бирюзовым
	if( m_level.terrain )
	{
		const ShaderView* mask = nullptr;
		if( m_showSoundSources )
		{
			std::vector<std::string> masks;
			for( const SoundArea& area : soundDebug().areas )
				masks.insert( masks.end(), area.masks.begin(), area.masks.end() );
			if( !masks.empty() )
				mask = m_soundAreas.update( m_terrainPipeline, std::move( masks ) );
		}
		m_terrain.setHighlight( mask, XMFLOAT3( 0.1f, 0.85f, 0.95f ) );
	}
}

SoundDebug Scene::soundDebug()
{
	SoundDebug debug;
	if( m_ambientSound.initialized() )
		m_ambientSound.soundDebug( debug );
	if( m_soundscape.initialized() )
		m_soundscape.soundDebug( debug );
	return debug;
}

const std::vector<SceneObject*>& Scene::objects() const
{
	return m_objects;
}

DMLightDriver& Scene::lights()
{
	return m_lightDriver;
}

ParticleSystem& Scene::particles()
{
	return m_particles;
}

const LevelDescription& Scene::level() const
{
	return m_level;
}

Wind& Scene::wind()
{
	return m_wind;
}

LevelSaveData Scene::levelSaveData( const PostProcessSettings& postProcess, const std::optional<HeightFogSettings>& heightFog )
{
	LevelSaveData data;
	// Источники — копией: id новых строк вернутся в неё и уйдут в источники
	data.lights = m_lightDriver.lights();
	data.removedLights = m_lightDriver.removedIds();
	for( size_t i = 0; i < data.lights.size(); ++i )
	{
		data.directionFromTime.push_back( m_lightDriver.directionFromTime( i ) );
		data.intensityFromTime.push_back( m_lightDriver.moonFromTime( i ) );
	}
	if( const SunPosition* position = m_lightDriver.sunPosition() )
		data.sunPosition = position->settings();
	// Небо — строка того, что у уровня работает: атмосферы или панорамы
	if( m_useHDRI )
		data.hdri = m_hdri.settings();
	else
		data.atmosphere = m_atmosphere.settings();
	if( m_clouds.initialized() )
		data.cloud = m_clouds.settings();
	data.postProcess = postProcess;
	data.wind = m_wind.settings();
	data.heightFog = heightFog;

	// Экземпляры — копией: id новых строк вернутся в неё и уйдут в экземпляры
	data.models = m_models.instances();
	data.removedModels = m_models.removedIds();
	if( m_level.terrain )
	{
		data.terrain = m_terrain.settings();
		m_terrainPipeline.settings( *data.terrain );
	}
	if( m_farTerrain.initialized() )
		data.farTerrain = m_farTerrain.settings();
	for( const auto& scatterer : m_scatterers )
	{
		data.scatterSets.push_back( scatterer->settings() );
		std::vector<ScatterLayerRecord> layers = scatterer->layerRecords();
		data.scatterLayers.insert( data.scatterLayers.end(), layers.begin(), layers.end() );
	}
	if( m_level.terrain && m_level.waterSimulation )
	{
		data.water = m_water.settings();
		data.water->streams = m_water.streamCurves();
	}
	if( m_particles.initialized() )
		data.emitters = m_particles.emitterSettings();
	if( m_reflectionCaptures.initialized() )
		data.reflectionCaptures = m_reflectionCaptures.captureSettings();
	if( m_ambientSound.initialized() )
		data.ambientSound = m_ambientSound.settings();
	if( m_soundscape.initialized() )
	{
		// Слои — копией: id новых строк вернутся в неё и уйдут в слои
		data.soundscapeLayers = m_soundscape.layerSettings();
		data.removedSoundscapeLayers = m_soundscape.removedIds();
		data.removedSoundRules = m_soundscape.removedRuleIds();
		data.sounds = m_soundscape.soundRecords();
	}
	return data;
}

void Scene::markLevelSaved( const PostProcessSettings& postProcess, const std::optional<HeightFogSettings>& heightFog )
{
	m_savedLevel = levelSaveData( postProcess, heightFog );
	// Экземпляры моделей — как в базе: пересадка на новую землю после эрозии (ModelInstances::reseat при загрузке) —
	// правка, её закрепляет «Save level»
	m_savedLevel.models = m_level.modelInstances;
}

bool Scene::saveLevel( LibraryLoader& library, const PostProcessSettings& postProcess,
						const std::optional<HeightFogSettings>& heightFog )
{
	LevelSaveData data = levelSaveData( postProcess, heightFog );
	if( !library.saveLevelEnvironment( m_level, data, m_savedLevel ) )
		return false;
	m_lightDriver.markSaved( data.lights );
	if( !library.saveLevelScene( m_level, data, m_savedLevel ) )
		return false;
	m_models.markSaved( data.models );
	if( m_soundscape.initialized() )
		m_soundscape.markSaved( data.soundscapeLayers );
	// Параметры экземпляров материалов (окна «Models» и «Materials» слоёв расстановки) — изменённые
	if( !library.saveMaterialInstances( Assets::materialInstances() ) )
		return false;
	// Экземпляры моделей в базе — на нынешней земле: пересадка после эрозии закреплена
	if( m_level.terrain )
		m_terrainPipeline.confirmErosionChange();
	// Сохранённое — новое прежнее: следующее «Save level» пишет правки после него. У заблокированного пресета окружение не
	// записано — для него прежний снимок остаётся («Save preset as» перенесёт правки в новый пресет)
	data.removedLights.clear();
	data.removedModels.clear();
	data.removedSoundscapeLayers.clear();
	data.removedSoundRules.clear();
	if( m_level.presetLocked )
		copyEnvironment( m_savedLevel, data );
	m_savedLevel = std::move( data );
	return true;
}

bool Scene::selectPreset( LibraryLoader& library, const std::string& preset )
{
	if( !preset.empty() && std::find( m_level.presets.begin(), m_level.presets.end(), preset ) == m_level.presets.end() )
	{
		LOG( "Preset '" + preset + "' is not found in table LevelPresets for level " + m_level.name );
		return false;
	}
	if( !library.selectLevelPreset( m_level, preset ) )
		return false;
	if( SunPosition* position = m_lightDriver.sunPosition(); position && m_level.sunPosition )
		position->apply( *m_level.sunPosition );
	if( !m_useHDRI )
		m_atmosphere.apply( m_level.atmosphere );
	if( m_clouds.initialized() && m_level.volumetricCloud )
		m_clouds.apply( *m_level.volumetricCloud );
	m_wind.apply( m_level.wind.value_or( WindSettings() ) );
	markEnvironmentWindowsSaved();
	return true;
}

bool Scene::savePreset( LibraryLoader& library, const std::string& name, const PostProcessSettings& postProcess,
						const std::optional<HeightFogSettings>& heightFog )
{
	LevelSaveData data = levelSaveData( postProcess, heightFog );
	if( !library.saveLevelPreset( m_level, name, data, m_savedLevel ) )
		return false;
	copyEnvironment( data, m_savedLevel );
	markEnvironmentWindowsSaved();
	return true;
}

void Scene::markEnvironmentSaved( const PostProcessSettings& postProcess, const std::optional<HeightFogSettings>& heightFog )
{
	copyEnvironment( levelSaveData( postProcess, heightFog ), m_savedLevel );
}

void Scene::markEnvironmentWindowsSaved()
{
	if( SunPosition* position = m_lightDriver.sunPosition() )
		position->properties()->markSaved();
	if( !m_useHDRI && m_atmosphere.properties() )
		m_atmosphere.properties()->markSaved();
	if( m_clouds.initialized() )
		m_clouds.properties()->markSaved();
	m_wind.properties()->markSaved();
}

bool Scene::rebuildChannels()
{
	const auto start = std::chrono::high_resolution_clock::now();
	WaterSimulationSettings water = m_water.settings();
	water.streams = m_water.streamCurves();
	if( !m_terrainPipeline.rebuildChannels( water, water.streams ) || !m_terrainPipeline.hydrology() ||
		!m_water.rebuild( *m_terrainPipeline.hydrology() ) )
	{
		LOG( "Water channels are not rebuilt" );
		return false;
	}
	// Постоянные слои (лес) разложены по прежней земле и воде — раскладка заново; трава раскладывается каждый кадр; камни
	// русел — новые
	for( const auto& scatterer : m_scatterers )
	{
		scatterer->setStreamStones( m_terrainPipeline.hydrology()->stones );
		scatterer->terrainChanged();
	}
	LOG( "Water channels rebuilt without reload, ms: " + elapsedMs( start ) );
	return true;
}

std::vector<WindObstacle> Scene::windObstacles() const
{
	std::vector<WindObstacle> obstacles;
	for( const DirectX::BoundingBox& bounds : m_models.tallBounds( 3.0f ) )
		obstacles.push_back( { bounds.Center.x, bounds.Center.z, std::max( bounds.Extents.x, bounds.Extents.z ) } );
	return obstacles;
}

void Scene::frameConstants( const RenderView& view, FrameParameters& constants )
{
	constants.skyLightScale = skyLightScale();
	constants.skyScale = skyScale();
	// Слои объёма воздушной перспективы — до AERIAL_PERSPECTIVE_DISTANCE (64 км, дальние горы), а не до дальней плоскости
	constants.aerialPerspectiveDistance = hasAtmosphere() ? AERIAL_PERSPECTIVE_DISTANCE : 0.0f;
	constants.aerialPerspectiveScale = aerialPerspectiveScale();
	constants.wind = m_wind.parameters();
	for( SceneObject* object : m_objects )
		object->frameConstants( view, constants );
	// Поле ветра по рельефу даёт конвейер (TerrainWind); флажок «Use field» окна «Wind» выключает его для сравнения
	if( !m_wind.fieldEnabled() )
		constants.wind.fieldScale = 0.0f;
}

DirectX::BoundingBox Scene::bounds() const
{
	DirectX::BoundingBox result( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ) );
	bool empty = true;
	if( m_level.terrain )
	{
		// Высота рельефа — значение карты 0…1, умноженное на множитель, плюс смещение
		const TerrainHeight height = m_terrainPipeline.terrainHeight();
		const float low = height.heightOffset + std::min( height.heightMultiplier, 0.0f );
		const float high = height.heightOffset + std::max( height.heightMultiplier, 0.0f );
		const XMFLOAT3 boxMin( 0.0f, low, 0.0f );
		const XMFLOAT3 boxMax( height.worldSize, high, height.worldSize );
		DirectX::BoundingBox::CreateFromPoints( result, XMLoadFloat3( &boxMin ), XMLoadFloat3( &boxMax ) );
		empty = false;
	}

	DirectX::BoundingBox models;
	if( m_models.bounds( models ) )
	{
		if( empty )
			result = models;
		else
			DirectX::BoundingBox::CreateMerged( result, result, models );
	}
	return result;
}

CDLODTerrain& Scene::terrain()
{
	return m_terrain;
}

TerrainPipeline& Scene::terrainPipeline()
{
	return m_terrainPipeline;
}

const TerrainHeightSource& Scene::terrainHeightSource() const
{
	return m_terrainPipeline;
}

TerrainPaintTool& Scene::terrainPaint()
{
	return m_terrainPaint;
}

ModelInstances& Scene::models()
{
	return m_models;
}

WaterSimulation& Scene::water()
{
	return m_water;
}

const std::vector<std::unique_ptr<Scatterer>>& Scene::scatterers() const
{
	return m_scatterers;
}

}
