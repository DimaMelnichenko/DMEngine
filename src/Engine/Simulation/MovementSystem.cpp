#include "MovementSystem.h"
#include "Components.h"
#include "Engine\Animation\AnimationSystem.h"
#include "Terrain\TerrainHeightSource.h"

using namespace DirectX;

namespace GS
{

void MovementSystem::step( World& world, float seconds )
{
	entt::registry& registry = world.registry();
	PhysicsWorld& physics = world.physics();
	const TerrainHeightSource* terrain = world.terrain();
	for( auto [entity, transform, locomotion] : registry.view<Transform, const Locomotion>().each() )
	{
		// Клип-действие держит место: желаемая скорость и взгляд не действуют (сила тяжести — как обычно)
		const AnimationState* animation = registry.try_get<AnimationState>( entity );
		const bool held = animation && holdsPlace( *animation );
		const XMFLOAT2 velocity = held ? XMFLOAT2( 0.0f, 0.0f ) : locomotion.velocity;
		if( const Character* character = registry.try_get<Character>( entity ) )
		{
			PhysicsWorld::CharacterMove move;
			move.velocity = velocity;
			move.gravity = gravity;
			transform.position = physics.moveCharacter( character->id, move, seconds ).feet;
		}
		else
		{
			transform.position.x += velocity.x * seconds;
			transform.position.z += velocity.y * seconds;
			float ground;
			if( terrain && terrain->surfaceHeight( transform.position.x, transform.position.z, ground ) )
				transform.position.y = ground;
		}
		// Взгляд по ходу: поворот вокруг вертикали (+Z к +X)
		if( !held )
			XMStoreFloat4( &transform.rotation, XMQuaternionRotationRollPitchYaw( 0.0f, locomotion.heading, 0.0f ) );
	}
}

}
