#include "WanderSystem.h"
#include "Components.h"
#include "Terrain\TerrainHeightSource.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace GS
{

namespace
{

constexpr float pi = 3.14159265358979f;

// −1…1 по xorshift32 (Marsaglia 2003)
float randomSigned( uint32_t& state )
{
	if( state == 0 )
		state = 0x9E3779B9u;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return static_cast<float>( state ) / 2147483648.0f - 1.0f;
}

// Угол в −π…π
float wrapAngle( float angle )
{
	return std::remainder( angle, 2.0f * pi );
}

float smoothstep( float from, float to, float x )
{
	const float t = std::clamp( ( x - from ) / std::max( to - from, 1e-6f ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

}

void WanderSystem::step( World& world, float seconds )
{
	const TerrainHeightSource* terrain = world.terrain();
	for( auto [entity, transform, wander] : world.registry().view<Transform, Wander>().each() )
	{
		// Точка блуждания сдвигается по окружности случайно — направление меняется плавно, без рывков; не дальше
		// четверти окружности от хода, иначе сущность кружит на месте
		wander.wanderAngle = std::clamp( wander.wanderAngle + randomSigned( wander.random ) * jitter * seconds, -0.5f * pi, 0.5f * pi );
		float desired = wander.heading + std::atan2( circleRadius * std::sin( wander.wanderAngle ),
													 circleDistance + circleRadius * std::cos( wander.wanderAngle ) );

		// Удержание в круге: за полосу в диаметр поворота от края — к центру, у самого края — только к центру
		const float dx = transform.position.x - wander.center.x;
		const float dz = transform.position.z - wander.center.y;
		const float distance = std::sqrt( dx * dx + dz * dz );
		const float turnDiameter = 2.0f * wander.speed / std::max( wander.turnRate, 1e-3f );
		const float margin = std::min( 0.5f * wander.radius, turnDiameter );
		const float home = smoothstep( wander.radius - 2.0f * margin, wander.radius - margin, distance );
		if( home > 0.0f && distance > 1e-4f )
			desired += wrapAngle( std::atan2( -dx, -dz ) - desired ) * home;

		const float maxTurn = wander.turnRate * seconds;
		wander.heading = wrapAngle( wander.heading + std::clamp( wrapAngle( desired - wander.heading ), -maxTurn, maxTurn ) );
		float x = transform.position.x + std::sin( wander.heading ) * wander.speed * seconds;
		float z = transform.position.z + std::cos( wander.heading ) * wander.speed * seconds;
		// Последняя защита: не дальше края круга
		const float ox = x - wander.center.x;
		const float oz = z - wander.center.y;
		const float outside = std::sqrt( ox * ox + oz * oz );
		if( outside > wander.radius )
		{
			x = wander.center.x + ox * wander.radius / outside;
			z = wander.center.y + oz * wander.radius / outside;
		}
		transform.position.x = x;
		transform.position.z = z;
		float ground;
		if( terrain && terrain->surfaceHeight( x, z, ground ) )
			transform.position.y = ground;
		// Взгляд по ходу: поворот вокруг вертикали (+Z к +X)
		XMStoreFloat4( &transform.rotation, XMQuaternionRotationRollPitchYaw( 0.0f, wander.heading, 0.0f ) );
	}
}

}
