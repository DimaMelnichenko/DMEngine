#include "MovementSystem.h"
#include "Components.h"
#include "Engine\Animation\AnimationSystem.h"
#include "Engine\Animation\AnimationLibrary.h"
#include "Terrain\TerrainHeightSource.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace GS
{

void MovementSystem::step( World& world, float seconds )
{
	entt::registry& registry = world.registry();
	PhysicsWorld& physics = world.physics();
	const TerrainHeightSource* terrain = world.terrain();
	const AnimationLibrary& animations = world.animations();
	for( auto [entity, transform, locomotion] : registry.view<Transform, const Locomotion>().each() )
	{
		// Клип-действие ведёт сущность сам (Root Motion монтажа в UE): желаемая скорость и взгляд не действуют, место и поворот
		// — движением корня клипа за шаг (клип без него — стоит; сила тяжести — как обычно)
		const AnimationState* animation = registry.try_get<AnimationState>( entity );
		const bool held = animation && holdsPlace( *animation );
		XMFLOAT2 velocity = locomotion.velocity;
		float heading = locomotion.heading;
		if( held )
		{
			velocity = XMFLOAT2( 0.0f, 0.0f );
			heading = headingOf( transform.rotation );
			if( const AnimationClip* clip = animations.clip( animation->current.clip ) )
			{
				// Перед следующим в очереди клип играет один проход — движение корня не дальше его конца
				double end = animation->current.time + seconds;
				if( animation->queued > 0 )
					end = std::min( end, static_cast<double>( clip->duration() ) );
				const AnimationClip::RootMotion delta = clip->rootMotionDelta( animation->current.time, end );
				// Сдвиг в осях корня (x — вбок, z — вперёд) — в мир по взгляду сущности, × её масштаб
				const float c = std::cos( heading ), s = std::sin( heading );
				const float scale = transform.scale / seconds;
				velocity = XMFLOAT2( ( delta.x * c + delta.z * s ) * scale, ( -delta.x * s + delta.z * c ) * scale );
				heading += delta.yaw;
			}
		}
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
		// Взгляд: по ходу или по клипу — поворот вокруг вертикали (+Z к +X)
		XMStoreFloat4( &transform.rotation, XMQuaternionRotationRollPitchYaw( 0.0f, heading, 0.0f ) );
	}
}

}
