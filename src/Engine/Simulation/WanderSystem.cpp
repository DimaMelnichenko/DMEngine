#include "WanderSystem.h"
#include "Components.h"
#include "Engine\Animation\AnimationSystem.h"
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
	entt::registry& registry = world.registry();
	for( auto [entity, transform, wander, locomotion] : registry.view<const Transform, Wander, Locomotion>().each() )
	{
		// Стоит — скорость 0 или играет клип-действие (лечь, удар): поведение ждёт, направление не меняется (иначе сущность
		// вертится на месте), после — с того же направления
		const AnimationState* animation = registry.try_get<AnimationState>( entity );
		if( wander.speed <= 0.0f || ( animation && holdsPlace( *animation ) ) )
		{
			wander.moving = false;
			locomotion.velocity = XMFLOAT2( 0.0f, 0.0f );
			locomotion.heading = wander.heading;
			continue;
		}

		// Точка блуждания сдвигается по окружности случайно — направление меняется плавно, без рывков; не дальше
		// четверти окружности от хода, иначе сущность кружит на месте
		wander.wanderAngle = std::clamp( wander.wanderAngle + randomSigned( wander.random ) * jitter * seconds, -0.5f * pi, 0.5f * pi );
		// Упёрлась (прошлый шаг прошёл меньше трети пути: модель, другой персонаж, вода) — точка блуждания на четверть
		// окружности в сторону: сущность отворачивает, а не толкает препятствие
		if( const PreviousTransform* previous = registry.try_get<PreviousTransform>( entity ) )
		{
			const float px = transform.position.x - previous->transform.position.x;
			const float pz = transform.position.z - previous->transform.position.z;
			if( wander.moving && px * px + pz * pz < 0.1f * wander.speed * seconds * wander.speed * seconds )
				wander.wanderAngle = randomSigned( wander.random ) > 0.0f ? 0.5f * pi : -0.5f * pi;
		}
		wander.moving = true;
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

		// Поворот не быстрее turnRate; за краем круга (столкнули) — сразу к центру
		const float maxTurn = wander.turnRate * seconds;
		if( distance > wander.radius )
			wander.heading = std::atan2( -dx, -dz );
		else
			wander.heading = wrapAngle( wander.heading + std::clamp( wrapAngle( desired - wander.heading ), -maxTurn, maxTurn ) );
		locomotion.velocity = XMFLOAT2( std::sin( wander.heading ) * wander.speed, std::cos( wander.heading ) * wander.speed );
		locomotion.heading = wander.heading;
	}
}

}
