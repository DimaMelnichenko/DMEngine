#include "Scene.h"
#include <chrono>
#include "System.h"
#include "Logger\Logger.h"

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
			if( !library.loadModelWithLOD( layer.model ) )
				return false;
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
	if( !m_lightDriver.loadFromFile( "Scene\\Lights.ini" ) )
		LOG( "Lights are not loaded from Scene\\Lights.ini, default light is used" );

	// Небо освещает сцену всегда, а фоном рисуется, если у уровня нет своей модели неба
	if( !m_atmosphere.initialize( m_lightDriver, "Scene\\Lights.ini" ) )
	{
		LOG( "Fail to initialize sky atmosphere" );
		return false;
	}
	m_atmosphere.setBackgroundVisible( !m_level.sky );

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
		scatterer->setColorTexture( set.colorTexture );

		for( const LevelDescription::ScatterLayer& layer : set.layers )
		{
			DMModel::LodBlock* block = System::models().get( layer.model )->getLodById( layer.modelLod );
			if( !block )
			{
				LOG( "Scatter set " + set.name + ": model " + std::to_string( layer.model ) + " has no LOD " + std::to_string( layer.modelLod ) );
				return false;
			}
			if( !scatterer->addLayer( block, layer.mask, layer.params ) )
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

	// Атмосфера первой: её compute() готовит освещение окружением для всех, render() рисует фон
	m_objects = { &m_atmosphere, &m_sky, &m_terrain, &m_models };
	for( const auto& scatterer : m_scatterers )
		m_objects.push_back( scatterer.get() );
	m_objects.push_back( &m_particles );

	return true;
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

CDLODTerrain& Scene::terrain()
{
	return m_terrain;
}

const std::vector<std::unique_ptr<Scatterer>>& Scene::scatterers() const
{
	return m_scatterers;
}

}
