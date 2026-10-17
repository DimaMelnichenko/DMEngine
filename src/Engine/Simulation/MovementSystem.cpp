#include "MovementSystem.h"
#include "Components.h"
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
		if( const Character* character = registry.try_get<Character>( entity ) )
		{
			PhysicsWorld::CharacterMove move;
			move.velocity = locomotion.velocity;
			move.gravity = gravity;
			transform.position = physics.moveCharacter( character->id, move, seconds ).feet;
		}
		else
		{
			transform.position.x += locomotion.velocity.x * seconds;
			transform.position.z += locomotion.velocity.y * seconds;
			float ground;
			if( terrain && terrain->surfaceHeight( transform.position.x, transform.position.z, ground ) )
				transform.position.y = ground;
		}
		// Взгляд по ходу: поворот вокруг вертикали (+Z к +X)
		XMStoreFloat4( &transform.rotation, XMQuaternionRotationRollPitchYaw( 0.0f, locomotion.heading, 0.0f ) );
	}
}

}
