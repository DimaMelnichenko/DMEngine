// Блуждание в круге (WanderSystem): сущность не уходит из круга, обходит его весь, стоит на земле, идёт с заданной
// скоростью и смотрит, куда идёт; путь повторяется при том же зерне. Рельеф — подделка (склон)
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include "Engine\Simulation\World.h"
#include "Engine\Simulation\Components.h"
#include "Engine\Simulation\WanderSystem.h"
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
	registry.emplace<Wander>( entity, wander );
	return entity;
}

void setUp( World& world )
{
	world.setTerrain( &slope );
	world.addSystem( std::make_unique<WanderSystem>() );
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
	CHECK( farthest <= 12.0f + 1e-3f );
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
