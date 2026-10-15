#include "QuickAdd.h"
#include <algorithm>
#include <cmath>
#include "GUI.h"
#include "Viewport.h"
#include "Logger\Logger.h"
#include "Scene\Camera\DMCamera.h"
#include "Scene\Scene.h"

using namespace DirectX;
using namespace GS;

void QuickAdd::initialize( DMCamera& camera, Scene& scene, Viewport& viewport, GUI& gui )
{
	m_camera = &camera;
	m_scene = &scene;
	m_viewport = &viewport;
	m_gui = &gui;
	// Источники света добавляются кнопками окна «Lights» (удаляются — кнопкой Delete своего подокна, DMLightDriver)
	for( DMLight::LightType type : { DMLight::Dir, DMLight::Point, DMLight::Spot } )
	{
		scene.lights().properties()->addButton( std::string( "Add " ) + DMLight::typeName( type ) + " light",
												"New light in front of the camera, selected in the viewport (Save level writes it, Ctrl+Z removes it)",
												[this, type] { return addLight( type ); } );
	}
	// Экземпляры моделей — кнопками подокна «Add» (модель палитры перед камерой), убираются — кнопкой Delete своего подокна
	// (ModelInstances)
	for( uint32_t model : scene.models().palette() )
	{
		scene.models().addProperties()->addButton( scene.models().modelName( model ),
												   "New instance of the model in front of the camera, on the ground (Save level writes it, Ctrl+Z removes it)",
												   [this, model] { return addModelInstance( model ); } );
	}
}

bool QuickAdd::placementInFront( XMFLOAT3& position ) const
{
	const XMFLOAT3 eye = m_camera->position();
	XMFLOAT3 forward;
	m_camera->viewDirection( &forward );
	const TerrainHeightSource& terrain = m_scene->terrainHeightSource();
	float ground = 0.0f;
	for( float t = 0.5f; t < 200.0f; t += 0.5f )
	{
		if( terrain.surfaceHeight( eye.x + forward.x * t, eye.z + forward.z * t, ground ) && eye.y + forward.y * t <= ground )
		{
			position = XMFLOAT3( eye.x + forward.x * t, ground, eye.z + forward.z * t );
			return true;
		}
	}
	position = XMFLOAT3( eye.x + forward.x * 10.0f, eye.y + forward.y * 10.0f, eye.z + forward.z * 10.0f );
	return false;
}

PropertyContainer::ButtonAction QuickAdd::addLight( DMLight::LightType type )
{
	DMLightDriver& lights = m_scene->lights();
	XMFLOAT3 forward;
	m_camera->viewDirection( &forward );
	XMFLOAT3 position;
	placementInFront( position );
	float ground = 0.0f;
	if( m_scene->terrainHeightSource().surfaceHeight( position.x, position.z, ground ) )
		position.y = std::max( position.y, ground + ( type == DMLight::Spot ? 3.0f : 1.5f ) );
	XMFLOAT3 direction( 0.0f, -1.0f, 0.0f );
	const float horizontal = std::sqrt( forward.x * forward.x + forward.z * forward.z );
	if( type == DMLight::Dir && horizontal > 1e-3f )
	{
		const float component = 1.0f / std::sqrt( 2.0f );
		direction = XMFLOAT3( forward.x / horizontal * component, -component, forward.z / horizontal * component );
	}

	const uint32_t key = lights.addLight( lights.newLight( type, position, direction ) );
	m_viewport->selectLight( lights, key );
	PropertyContainer* properties = lights.lightProperties( lights.indexOf( key ) );
	if( properties )
		m_gui->focusProperties( properties );
	LOG( "Light added: " + ( properties ? properties->name() : std::string() ) );
	return { [&lights, key] { lights.removeLight( key ); },
			 [this, &lights, key]
			 {
				 lights.restoreLight( key );
				 m_viewport->selectLight( lights, key );
			 },
			 "Add " + ( properties ? properties->name() : std::string( DMLight::typeName( type ) ) ) };
}

PropertyContainer::ButtonAction QuickAdd::addModelInstance( uint32_t model )
{
	ModelInstances& models = m_scene->models();
	XMFLOAT3 position;
	const bool onGround = placementInFront( position );
	const uint32_t key = models.addInstance( model, position, onGround ? &m_scene->terrainHeightSource() : nullptr );
	const int index = models.indexOf( key );
	PropertyContainer* properties = models.instanceProperties( index );
	m_viewport->selectInstance( models, key );
	m_gui->focusProperties( properties );
	const std::string name = models.instanceName( index );
	LOG( "Model instance added: " + name );
	return { [&models, key] { models.removeInstance( key ); },
			 [this, &models, key]
			 {
				 models.restoreInstance( key );
				 m_viewport->selectInstance( models, key );
			 },
			 "Add " + name };
}
