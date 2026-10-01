#include "Scene.h"
#include "ObjectLibrary\LibraryLoader.h"
#include <algorithm>
#include <chrono>
#include "System.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace
{

std::string elapsedMs( std::chrono::high_resolution_clock::time_point start )
{
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::high_resolution_clock::now() - start );
	return std::to_string( elapsed.count() / 1000.0 );
}

}

namespace GS
{

bool Scene::loadResources( LibraryLoader& library, const std::string& levelName )
{
	LOG( "Load materials" );
	auto timeStart = std::chrono::high_resolution_clock::now();
	if( !library.loadAllMaterials() )
		return false;
	LOG( "Load material for ms: " + elapsedMs( timeStart ) );

	if( !library.loadLevel( levelName, m_level ) )
		return false;
	LOG( "Level: " + m_level.name );

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
	LOG( "Load models for ms: " + elapsedMs( timeStart ) );

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

bool Scene::initialize()
{
	LOG( "Create light driver" );
	if( !m_lightDriver.Initialize() )
		return false;
	if( m_level.lights.empty() )
		LOG( "Level has no lights in table LevelLights, default light is used" );
	m_lightDriver.load( m_level.lights, m_level.sunPosition );
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
		if( !m_atmosphere.initialize( m_lightDriver, m_level.atmosphere, m_skyLight ) )
		{
			LOG( "Fail to initialize sky atmosphere" );
			return false;
		}
		m_atmosphere.setBackgroundVisible( !m_level.sky );
	}
	m_wind.initialize( m_level.wind );
	updateLights();

	// Расстановка и частицы стоят на террейне и читают его карту высот
	std::string heightMap;
	auto timeStart = std::chrono::high_resolution_clock::now();
	if( m_level.terrain )
	{
		if( !m_terrain.initialize( *m_level.terrain ) )
			return false;
		heightMap = m_terrain.terrainHeight().heightMap;
		LOG( "Terrain init ms: " + elapsedMs( timeStart ) );
	}

	if( m_level.sky )
		m_sky.setModel( *m_level.sky );
	m_models.initialize( m_level.modelInstances );

	timeStart = std::chrono::high_resolution_clock::now();
	for( const LevelDescription::ScatterSet& set : m_level.scatterSets )
	{
		if( !m_level.terrain )
		{
			LOG( "Scatter set " + set.name + " needs a terrain, skipped" );
			continue;
		}

		auto scatterer = std::make_unique<Scatterer>( set.name );
		if( !scatterer->Initialize() )
			return false;
		scatterer->setTerrain( &m_terrain );

		for( const LevelDescription::ScatterLayer& layer : set.layers )
		{
			std::vector<Scatterer::LayerModel> models;
			for( const LevelDescription::ScatterModel& variant : layer.models )
			{
				DMModel* model = System::models().get( variant.model ).get();
				if( !model || model->lodCount() == 0 )
				{
					LOG( "Scatter set " + set.name + ": model " + std::to_string( variant.model ) + " has no LOD" );
					return false;
				}
				models.push_back( { model, variant.weight, variant.castShadow } );
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
	LOG( "Scatter init ms: " + elapsedMs( timeStart ) );

	if( m_level.particles )
	{
		const LevelDescription::Particles& particles = *m_level.particles;
		if( !m_particles.Initialize( particles.countPerCell, particles.areaSize, heightMap, particles.material, particles.texture ) )
		{
			LOG( "Fail to initialize particle sytem" );
			return false;
		}
	}

	// Небо первым: его compute() готовит освещение окружением для всех, render() рисует фон
	SceneObject* sky = m_useHDRI ? static_cast<SceneObject*>( &m_hdri ) : &m_atmosphere;
	m_objects = { sky, &m_sky, &m_terrain, &m_models };
	for( const auto& scatterer : m_scatterers )
		m_objects.push_back( scatterer.get() );
	m_objects.push_back( &m_particles );

	return true;
}

void Scene::updateLights( float seconds )
{
	m_lightDriver.update( seconds );
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

void Scene::update( const FrameContext& frame )
{
	for( SceneObject* object : m_objects )
	{
		object->update( frame );
	}
}

const std::vector<SceneObject*>& Scene::objects() const
{
	return m_objects;
}

DMLightDriver& Scene::lights()
{
	return m_lightDriver;
}

const LevelDescription& Scene::level() const
{
	return m_level;
}

Wind& Scene::wind()
{
	return m_wind;
}

bool Scene::saveEnvironment( LibraryLoader& library, const PostProcessSettings& postProcess )
{
	std::optional<SunPosition::Settings> sunPosition;
	if( const SunPosition* position = m_lightDriver.sunPosition() )
		sunPosition = position->settings();
	// Небо — строка того, что у уровня работает: атмосферы или панорамы
	std::optional<SkyAtmosphere::Settings> atmosphere;
	std::optional<HDRIBackdrop::Settings> hdri;
	if( m_useHDRI )
		hdri = m_hdri.settings();
	else
		atmosphere = m_atmosphere.settings();
	return library.saveLevelEnvironment( m_level, m_lightDriver.lights(), sunPosition, atmosphere, hdri, postProcess,
										 m_wind.settings() );
}

DirectX::BoundingBox Scene::bounds() const
{
	DirectX::BoundingBox result( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ) );
	bool empty = true;
	if( m_level.terrain )
	{
		// Высота рельефа — значение карты 0…1, умноженное на множитель, плюс смещение
		const TerrainHeight height = m_terrain.terrainHeight();
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

const std::vector<std::unique_ptr<Scatterer>>& Scene::scatterers() const
{
	return m_scatterers;
}

}
