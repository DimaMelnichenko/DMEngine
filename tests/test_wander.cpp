// Блуждание в круге (WanderSystem) с передвижением по земле (MovementSystem, без персонажей): сущность не уходит из круга,
// обходит его весь, стоит на земле, идёт с заданной скоростью и смотрит, куда идёт; путь повторяется при том же зерне; при
// скорости 0 и клипе-действии (AnimationState) стоит и не поворачивается.
// Рельеф — подделка (склон)
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include "Engine\Simulation\World.h"
#include "Engine\Simulation\Components.h"
#include "Engine\Simulation\WanderSystem.h"
#include "Engine\Simulation\MovementSystem.h"
#include "Engine\Animation\AnimationSystem.h"
#include "fake_terrain.h"

using namespace GS;
using namespace DirectX;

namespace
{

const FakeTerrain slope( []( float x, float z ) { return 10.0f + 0.1f * x - 0.05f * z; } );
constexpr float step = 1.0f / 60.0f;

entt::entity spawn( World& world, uint32_t seed, float radius = 12.0f, float speed = 1.2f )
{
	Transform transform;
	transform.position = XMFLOAT3( 128.0f, 0.0f, 128.0f );
	Wander wander;
	wander.center = XMFLOAT2( 128.0f, 128.0f );
	wander.radius = radius;
	wander.speed = speed;
	wander.random = seed;
	entt::registry& registry = world.registry();
	const entt::entity entity = registry.create();
	registry.emplace<Transform>( entity, transform );
	registry.emplace<PreviousTransform>( entity, PreviousTransform{ transform } );
	registry.emplace<Wander>( entity, wander );
	registry.emplace<Locomotion>( entity );
	return entity;
}

void setUp( World& world )
{
	world.setTerrain( &slope );
	world.addSystem( std::make_unique<WanderSystem>() );
	world.addSystem( std::make_unique<MovementSystem>() );
}

}

TEST_CASE( "WanderSystem: ten minutes inside the circle, all over it, on the ground, at its speed" )
{
	World world;
	setUp( world );
	const entt::entity entity = spawn( world, 7 );
	const Transform& transform = world.registry().get<Transform>( entity );
	float farthest = 0.0f;
	float path = 0.0f;
	bool quadrants[4] = {};
	XMFLOAT3 previous = transform.position;
	for( int i = 0; i < 60 * 600; ++i )
	{
		world.tick( step );
		const float dx = transform.position.x - 128.0f;
		const float dz = transform.position.z - 128.0f;
		const float distance = std::sqrt( dx * dx + dz * dz );
		farthest = std::max( farthest, distance );
		if( distance > 6.0f )
			quadrants[( dx > 0.0f ? 1 : 0 ) + ( dz > 0.0f ? 2 : 0 )] = true;
		path += std::sqrt( ( transform.position.x - previous.x ) * ( transform.position.x - previous.x ) +
						   ( transform.position.z - previous.z ) * ( transform.position.z - previous.z ) );
		previous = transform.position;
	}
	CHECK( farthest <= 12.0f + 0.05f );					// за край — не дальше шага, затем сразу к центру
	CHECK( farthest > 9.0f );							// доходит до края, а не топчется у центра
	CHECK( ( quadrants[0] && quadrants[1] && quadrants[2] && quadrants[3] ) );
	float ground = 0.0f;
	REQUIRE( slope.surfaceHeight( transform.position.x, transform.position.z, ground ) );
	CHECK( transform.position.y == doctest::Approx( ground ) );
	CHECK( path / 600.0f == doctest::Approx( 1.2f ).epsilon( 0.02 ) );	// скорость — у края почти не срезается

	// Взгляд — по ходу: +Z модели, повёрнутый кватернионом, — направление последнего шага
	world.tick( step );
	const XMVECTOR forward = XMVector3Rotate( XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f ), XMLoadFloat4( &transform.rotation ) );
	const XMVECTOR moved = XMVector3Normalize( XMVectorSet( transform.position.x - previous.x, 0.0f, transform.position.z - previous.z, 0.0f ) );
	CHECK( XMVectorGetX( XMVector3Dot( forward, moved ) ) > 0.999f );
}

TEST_CASE( "WanderSystem: the same seed walks the same path, another seed - another" )
{
	World first, second, other;
	setUp( first );
	setUp( second );
	setUp( other );
	const entt::entity a = spawn( first, 42 );
	const entt::entity b = spawn( second, 42 );
	const entt::entity c = spawn( other, 43 );
	for( int i = 0; i < 60 * 30; ++i )
	{
		first.tick( step );
		second.tick( step );
		other.tick( step );
	}
	const XMFLOAT3& pa = first.registry().get<Transform>( a ).position;
	const XMFLOAT3& pb = second.registry().get<Transform>( b ).position;
	const XMFLOAT3& pc = other.registry().get<Transform>( c ).position;
	CHECK( pa.x == pb.x );
	CHECK( pa.z == pb.z );
	CHECK( std::abs( pa.x - pc.x ) + std::abs( pa.z - pc.z ) > 0.1f );
}

TEST_CASE( "WanderSystem: at zero speed the entity stands and does not turn" )
{
	World world;
	setUp( world );
	const entt::entity entity = spawn( world, 11, 12.0f, 0.0f );
	world.tick( step );
	const Transform before = world.registry().get<Transform>( entity );
	for( int i = 0; i < 60 * 5; ++i )
		world.tick( step );
	const Transform& after = world.registry().get<Transform>( entity );
	CHECK( after.position.x == before.position.x );
	CHECK( after.position.z == before.position.z );
	CHECK( after.rotation.y == doctest::Approx( before.rotation.y ) );
	CHECK( after.rotation.w == doctest::Approx( before.rotation.w ) );
}

TEST_CASE( "WanderSystem: an animation clip holds the place, the walk goes on from the same heading" )
{
	World world;
	setUp( world );
	const entt::entity entity = spawn( world, 5 );
	entt::registry& registry = world.registry();
	for( int i = 0; i < 60; ++i )
		world.tick( step );
	// Клип-действие (лечь): ни места, ни поворота
	AnimationState animation;
	animation.current = clipPlayback( 1 );
	registry.emplace<AnimationState>( entity, animation );
	const Transform held = registry.get<Transform>( entity );
	const float heading = registry.get<Wander>( entity ).heading;
	for( int i = 0; i < 60 * 3; ++i )
		world.tick( step );
	const Transform& lying = registry.get<Transform>( entity );
	CHECK( lying.position.x == held.position.x );
	CHECK( lying.position.z == held.position.z );
	CHECK( lying.rotation.y == held.rotation.y );
	CHECK( registry.get<Wander>( entity ).heading == heading );
	// Снова шаг по скорости: идёт дальше с того же направления, без рывка
	playAnimation( registry.get<AnimationState>( entity ), AnimationPlayback() );
	world.tick( step );
	CHECK( std::abs( registry.get<Wander>( entity ).heading - heading ) <= 1.5f * step + 1e-4f );
	for( int i = 0; i < 60; ++i )
		world.tick( step );
	const XMFLOAT3& walked = registry.get<Transform>( entity ).position;
	CHECK( std::hypot( walked.x - held.position.x, walked.z - held.position.z ) > 0.8f );
}

TEST_CASE( "WanderSystem: after an action that turned the entity, it walks on where it faces" )
{
	World world;
	setUp( world );
	const entt::entity entity = spawn( world, 9, 30.0f );
	entt::registry& registry = world.registry();
	for( int i = 0; i < 30; ++i )
		world.tick( step );
	// Действие: клип держит место, разворот — как движением корня (здесь — сразу на 2 рад)
	AnimationState animation;
	animation.current = clipPlayback( 1 );
	registry.emplace<AnimationState>( entity, animation );
	world.tick( step );
	Transform& transform = registry.get<Transform>( entity );
	const float turned = headingOf( transform.rotation ) + 2.0f;
	XMStoreFloat4( &transform.rotation, XMQuaternionRotationRollPitchYaw( 0.0f, turned, 0.0f ) );
	world.tick( step );
	// Снова шаг по скорости: блуждание — от нового направления, без рывка назад
	playAnimation( registry.get<AnimationState>( entity ), AnimationPlayback() );
	world.tick( step );
	const float heading = registry.get<Wander>( entity ).heading;
	CHECK( std::abs( std::remainder( heading - turned, 6.2831853f ) ) <= 1.5f * step + 1e-3f );
}
