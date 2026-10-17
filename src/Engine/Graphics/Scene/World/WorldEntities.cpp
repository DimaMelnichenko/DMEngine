#include "WorldEntities.h"
#include <algorithm>
#include <cmath>
#include "System.h"
#include "RenderView.h"
#include "Model\ModelDraw.h"
#include "Terrain\TerrainHeightSource.h"
#include "Engine\Simulation\Components.h"
#include "Engine\Simulation\WanderSystem.h"

using namespace DirectX;

namespace GS
{

WorldEntities::WorldEntities() :
	SceneObject( "World" )
{
	m_properties.setName( "World" );
	Property* rate = m_properties.insert( "Step rate", World::defaultStepRate );
	rate->setLow( 10.0f );
	rate->setHigh( 240.0f );
	rate->setUnit( "Hz" )->setTooltip( "Fixed steps of the simulation per second; frames interpolate between the steps" );
	m_properties.insert( "Simulation paused", false )->setTooltip( "Entities stand still; the world clock goes on" );
}

void WorldEntities::initialize( World& world )
{
	m_world = &world;
}

void WorldEntities::update( const FrameContext& frame )
{
	if( !m_world )
		return;
	m_world->setStepRate( m_properties["Step rate"].data<float>() );
	const bool paused = m_properties["Simulation paused"].data<bool>();
	if( paused != m_world->paused() )
		m_world->setPaused( paused );
}

void WorldEntities::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	// Съёмка сцены (карты отражений места) — без движущегося
	if( !m_world || view.kind == RenderView::Kind::capture )
		return;
	const float t = m_world->interpolation();
	const entt::registry& registry = m_world->registry();
	for( auto [entity, current, visual] : registry.view<const Transform, const ModelVisual>().each() )
	{
		const DMModel* model = System::models().get( visual.model ).get();
		if( !model || model->lodCount() == 0 )
			continue;
		const PreviousTransform* previous = registry.try_get<PreviousTransform>( entity );
		const Transform place = previous ? interpolate( previous->transform, current, t ) : current;
		// Точка места — на земле, низ модели — на ней
		const XMFLOAT3 origin( place.position.x, place.position.y + visual.groundOffset * place.scale, place.position.z );
		const XMMATRIX world = XMMatrixScaling( place.scale, place.scale, place.scale ) *
							   XMMatrixRotationQuaternion( XMLoadFloat4( &place.rotation ) ) *
							   XMMatrixTranslation( origin.x, origin.y, origin.z );
		collectModelMeshes( *model, visual.model, world, origin, view, collector );
	}
}

bool WorldEntities::spawnWanderers( const std::string& model, float x, float z, uint32_t count, float radius, std::string& error )
{
	if( !m_world )
	{
		error = "the world is not initialized";
		return false;
	}
	if( !System::models().exists( model ) )
	{
		error = "model " + model + " is not loaded (models of the level, its sky and scatter sets)";
		return false;
	}
	const uint32_t modelId = System::models().id( model );
	DMModel* asset = System::models().get( modelId ).get();
	const DMModel::LodBlock* lod = asset ? asset->getLodById( 0 ) : nullptr;
	if( !lod )
	{
		error = "model " + model + " has no LOD";
		return false;
	}

	entt::registry& registry = m_world->registry();
	const TerrainHeightSource* terrain = m_world->terrain();
	for( uint32_t i = 0; i < count; ++i )
	{
		// Место в круге — по золотому углу (ровно, без случайных кучек), направление и зерно — своё у каждой
		const uint32_t serial = ++m_spawned;
		const float angle = 2.39996323f * serial;
		const float distance = radius * 0.7f * std::sqrt( ( i + 0.5f ) / count );
		Transform transform;
		transform.position = XMFLOAT3( x + std::sin( angle ) * distance, 0.0f, z + std::cos( angle ) * distance );
		float ground;
		if( terrain && terrain->surfaceHeight( transform.position.x, transform.position.z, ground ) )
			transform.position.y = ground;

		Wander wander;
		wander.center = XMFLOAT2( x, z );
		wander.radius = radius;
		wander.heading = angle;
		wander.random = 0x9E3779B9u * serial;
		const entt::entity entity = registry.create();
		registry.emplace<Transform>( entity, transform );
		registry.emplace<PreviousTransform>( entity, PreviousTransform{ transform } );
		// Низ границ LOD 0 — на точку места
		registry.emplace<ModelVisual>( entity, ModelVisual{ modelId, -( lod->bounds.Center.y - lod->bounds.Extents.y ) } );
		registry.emplace<Wander>( entity, wander );
	}
	return true;
}

}
