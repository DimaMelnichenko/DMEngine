// Мир столкновений (PhysicsWorld, Jolt): земля совпадает с surfaceHeight (и в детальном квадрате), персонаж падает и стоит,
// стоящий не сползает по пологому склону (по крутому — съезжает),
// коробка, капсула и оболочка преграждают путь, персонажи не проходят друг сквозь друга, глубокая вода не пускает (вдоль
// берега — идёт, вброд — переходит), бродяги-персонажи не сходятся, персонажи убираются с сущностями, край карты держит
// персонажа, а лучи его не видят
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "Engine\Simulation\World.h"
#include "Engine\Simulation\Components.h"
#include "Engine\Simulation\MovementSystem.h"
#include "Engine\Simulation\WanderSystem.h"
#include "fake_terrain.h"

using namespace GS;
using namespace DirectX;

namespace
{

constexpr float step = 1.0f / 60.0f;
constexpr float flatHeight = 10.0f;

float flat( float, float )
{
	return flatHeight;
}

// Персонаж идёт со скоростью velocity seconds секунд; after — после каждого шага (ноги)
template<class After>
PhysicsWorld::CharacterState walk( PhysicsWorld& physics, PhysicsWorld::CharacterId character, XMFLOAT2 velocity, float seconds,
								   After after )
{
	PhysicsWorld::CharacterMove move;
	move.velocity = velocity;
	PhysicsWorld::CharacterState state;
	for( float t = 0.0f; t < seconds; t += step )
	{
		state = physics.moveCharacter( character, move, step );
		after( state.feet );
	}
	return state;
}

PhysicsWorld::CharacterState walk( PhysicsWorld& physics, PhysicsWorld::CharacterId character, XMFLOAT2 velocity, float seconds )
{
	return walk( physics, character, velocity, seconds, []( const XMFLOAT3& ) {} );
}

float horizontal( const XMFLOAT3& a, float x, float z )
{
	return std::sqrt( ( a.x - x ) * ( a.x - x ) + ( a.z - z ) * ( a.z - z ) );
}

}

TEST_CASE( "PhysicsWorld: the ground matches surfaceHeight, a fine region resolves a narrow trench" )
{
	// Холмы и в детальном квадрате [96, 128]² — жёлоб шириной 1 м и глубиной 1 м по x = 100,5 (изломы — на сетке 0,25 м)
	FakeTerrain terrain( []( float x, float z )
	{
		float height = 10.0f + 2.0f * std::sin( 0.1f * x ) + 1.5f * std::cos( 0.13f * z );
		if( z > 96.0f && z < 128.0f )
			height -= std::max( 0.0f, 1.0f - std::abs( x - 100.5f ) / 0.5f );
		return height;
	} );
	terrain.regions.push_back( { 96.0f, 96.0f, 32.0f, 0.25f } );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );

	auto ground = [&]( float x, float z )
	{
		float fraction = 0.0f;
		REQUIRE( physics.castRay( XMFLOAT3( x, 100.0f, z ), XMFLOAT3( 0.0f, -200.0f, 0.0f ), fraction ) );
		return 100.0f - 200.0f * fraction;
	};
	float worst = 0.0f;
	for( float x = 3.3f; x < 250.0f; x += 7.7f )
		for( float z = 5.1f; z < 250.0f; z += 9.3f )
		{
			float expected = 0.0f;
			terrain.surfaceHeight( x, z, expected );
			worst = std::max( worst, std::abs( ground( x, z ) - expected ) );
		}
	CHECK( worst < 0.01f );
	// Дно жёлоба — в квадрате, вне его карта 1 м жёлоба не видит
	float bottom = 0.0f;
	terrain.surfaceHeight( 100.5f, 110.0f, bottom );
	CHECK( ground( 100.5f, 110.0f ) == doctest::Approx( bottom ).epsilon( 0.001 ) );
	CHECK( ground( 100.75f, 120.0f ) < ground( 102.0f, 120.0f ) );
}

TEST_CASE( "PhysicsWorld: a character falls and stands on the ground" )
{
	FakeTerrain terrain( flat );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );
	const PhysicsWorld::CharacterId character = physics.createCharacter( {}, XMFLOAT3( 128.0f, 20.0f, 128.0f ) );
	const PhysicsWorld::CharacterState state = walk( physics, character, XMFLOAT2( 0.0f, 0.0f ), 3.0f );
	CHECK( std::abs( state.feet.y - flatHeight ) < 0.03f );
	CHECK( state.onGround );
}

TEST_CASE( "PhysicsWorld: a standing character does not slide down a gentle slope, down a steep one it does" )
{
	// Склон 15° — пологий (стоит: лежащий зверь не ползёт), 55° — круче «Max slope» 40° (съезжает)
	for( const float degrees : { 15.0f, 55.0f } )
	{
		const float tangent = std::tan( degrees * 3.14159265f / 180.0f );
		FakeTerrain terrain( [tangent]( float x, float ) { return 100.0f - tangent * x; } );
		PhysicsWorld physics;
		REQUIRE( physics.buildTerrain( terrain ) );
		const PhysicsWorld::CharacterId character =
			physics.createCharacter( {}, XMFLOAT3( 128.0f, 100.0f - tangent * 128.0f + 0.5f, 128.0f ) );
		const XMFLOAT3 settled = walk( physics, character, XMFLOAT2( 0.0f, 0.0f ), 1.0f ).feet;
		const XMFLOAT3 after = walk( physics, character, XMFLOAT2( 0.0f, 0.0f ), 3.0f ).feet;
		if( degrees < 40.0f )
			CHECK( horizontal( after, settled.x, settled.z ) < 0.01f );
		else
			CHECK( after.x - settled.x > 0.5f );
	}
}

TEST_CASE( "PhysicsWorld: a box, a capsule and a hull are not walked through" )
{
	FakeTerrain terrain( flat );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );
	const float radius = 0.3f;	// капсула персонажа по умолчанию

	// Коробка 4 × 2 × 1 м поперёк пути: перед ней и стоит
	physics.addStaticBox( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 2.0f, 1.0f, 0.5f ), XMMatrixTranslation( 128.0f, 11.0f, 135.0f ) );
	PhysicsWorld::CharacterId character = physics.createCharacter( {}, XMFLOAT3( 128.0f, flatHeight, 128.0f ) );
	PhysicsWorld::CharacterState state = walk( physics, character, XMFLOAT2( 0.0f, 1.5f ), 10.0f );
	CHECK( state.feet.z < 134.5f - radius + 0.05f );
	CHECK( state.feet.z > 133.5f );
	physics.removeCharacter( character );

	// Ствол — капсула радиусом 0,5 м: может обойти, но не пройти насквозь
	physics.addStaticCapsule( XMFLOAT3( 0.0f, 0.0f, 0.0f ), 6.0f, 0.5f, XMMatrixTranslation( 60.0f, flatHeight, 135.0f ) );
	character = physics.createCharacter( {}, XMFLOAT3( 60.0f, flatHeight, 128.0f ) );
	float closest = 1e9f;
	walk( physics, character, XMFLOAT2( 0.0f, 1.5f ), 10.0f, [&]( const XMFLOAT3& feet ) { closest = std::min( closest, horizontal( feet, 60.0f, 135.0f ) ); } );
	CHECK( closest > 0.5f + radius - 0.05f );
	physics.removeCharacter( character );

	// Шар — оболочка точек сферы радиусом 1 м с центром на 1 м над землёй
	std::vector<XMFLOAT3> sphere;
	for( int i = 0; i < 12; ++i )
		for( int j = 0; j <= 6; ++j )
		{
			const float a = i * XM_2PI / 12.0f, b = j * XM_PI / 6.0f - XM_PIDIV2;
			sphere.push_back( XMFLOAT3( std::cos( b ) * std::cos( a ), std::sin( b ), std::cos( b ) * std::sin( a ) ) );
		}
	physics.addStaticHull( sphere, XMMatrixTranslation( 190.0f, flatHeight + 1.0f, 135.0f ) );
	character = physics.createCharacter( {}, XMFLOAT3( 190.0f, flatHeight, 128.0f ) );
	closest = 1e9f;
	walk( physics, character, XMFLOAT2( 0.0f, 1.5f ), 10.0f, [&]( const XMFLOAT3& feet ) { closest = std::min( closest, horizontal( feet, 190.0f, 135.0f ) ); } );
	CHECK( closest > 0.95f + radius - 0.05f );	// оболочка 12 × 7 точек — чуть внутри сферы
	CHECK( physics.bodyCount() == 3 );
}

TEST_CASE( "PhysicsWorld: two characters don't walk through each other" )
{
	FakeTerrain terrain( flat );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );
	const PhysicsWorld::CharacterId a = physics.createCharacter( {}, XMFLOAT3( 128.0f, flatHeight, 120.0f ) );
	const PhysicsWorld::CharacterId b = physics.createCharacter( {}, XMFLOAT3( 128.0f, flatHeight, 136.0f ) );
	PhysicsWorld::CharacterMove toward;
	toward.velocity = XMFLOAT2( 0.0f, 1.5f );
	PhysicsWorld::CharacterMove back;
	back.velocity = XMFLOAT2( 0.0f, -1.5f );
	float closest = 1e9f;
	for( float t = 0.0f; t < 10.0f; t += step )
	{
		const XMFLOAT3 pa = physics.moveCharacter( a, toward, step ).feet;
		const XMFLOAT3 pb = physics.moveCharacter( b, back, step ).feet;
		closest = std::min( closest, horizontal( pa, pb.x, pb.z ) );
	}
	CHECK( closest > 2.0f * 0.3f - 0.05f );
}

TEST_CASE( "PhysicsWorld: deep water stops, along the shore and through shallow water a character goes" )
{
	// За z = 140 — вода: глубина 1,5 м (глубже брода 1 м) при x < 128, 0,5 м — при x ≥ 128
	FakeTerrain terrain( flat );
	terrain.water = []( float x, float z ) { return z > 140.0f ? ( x < 128.0f ? flatHeight + 1.5f : flatHeight + 0.5f ) : -1e30f; };
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );

	// Прямо в глубокую — стоит у берега
	PhysicsWorld::CharacterId character = physics.createCharacter( {}, XMFLOAT3( 80.0f, flatHeight, 130.0f ) );
	PhysicsWorld::CharacterState state = walk( physics, character, XMFLOAT2( 0.0f, 1.5f ), 10.0f );
	CHECK( state.feet.z < 140.0f );
	CHECK( state.feet.z > 138.5f );
	physics.removeCharacter( character );

	// Наискось к глубокой — идёт вдоль берега
	character = physics.createCharacter( {}, XMFLOAT3( 60.0f, flatHeight, 136.0f ) );
	state = walk( physics, character, XMFLOAT2( 1.0f, 1.0f ), 10.0f );
	CHECK( state.feet.z < 140.0f );
	CHECK( state.feet.x > 65.0f );
	physics.removeCharacter( character );

	// В мелкую — переходит вброд
	character = physics.createCharacter( {}, XMFLOAT3( 180.0f, flatHeight, 130.0f ) );
	state = walk( physics, character, XMFLOAT2( 0.0f, 1.5f ), 10.0f );
	CHECK( state.feet.z > 142.0f );
}

TEST_CASE( "PhysicsWorld: wandering characters keep apart, entities take their characters away" )
{
	FakeTerrain terrain( flat );
	World world;
	world.setTerrain( &terrain );
	REQUIRE( world.physics().buildTerrain( terrain ) );
	world.addSystem( std::make_unique<WanderSystem>() );
	world.addSystem( std::make_unique<MovementSystem>() );

	entt::registry& registry = world.registry();
	std::vector<entt::entity> entities;
	PhysicsWorld::CharacterSettings settings;
	settings.radius = 0.5f;
	settings.height = 1.0f;
	for( uint32_t i = 0; i < 6; ++i )
	{
		Transform transform;
		transform.position = XMFLOAT3( 128.0f + 2.0f * std::sin( i * 1.05f ), flatHeight, 128.0f + 2.0f * std::cos( i * 1.05f ) );
		Wander wander;
		wander.center = XMFLOAT2( 128.0f, 128.0f );
		wander.radius = 4.0f;
		wander.random = 11u + i;
		const entt::entity entity = registry.create();
		registry.emplace<Transform>( entity, transform );
		registry.emplace<PreviousTransform>( entity, PreviousTransform{ transform } );
		registry.emplace<Wander>( entity, wander );
		registry.emplace<Locomotion>( entity );
		registry.emplace<Character>( entity, Character{ world.physics().createCharacter( settings, transform.position ) } );
		entities.push_back( entity );
	}
	float closest = 1e9f;
	for( int frame = 0; frame < 60 * 60; ++frame )
	{
		world.tick( step );
		for( size_t a = 0; a < entities.size(); ++a )
			for( size_t b = a + 1; b < entities.size(); ++b )
			{
				const XMFLOAT3& pa = registry.get<Transform>( entities[a] ).position;
				const XMFLOAT3& pb = registry.get<Transform>( entities[b] ).position;
				closest = std::min( closest, horizontal( pa, pb.x, pb.z ) );
			}
	}
	CHECK( closest > 2.0f * settings.radius - 0.1f );
	CHECK( world.physics().characterCount() == 6 );
	world.clear();
	CHECK( world.physics().characterCount() == 0 );
}

TEST_CASE( "PhysicsWorld: a shared hull shape takes the scale and rotation of each body" )
{
	PhysicsWorld physics;
	// Куб 1 м: общая форма, тела — масштаб 2, второе ещё повёрнуто на 45° вокруг вертикали
	std::vector<XMFLOAT3> cube;
	for( int i = 0; i < 8; ++i )
		cube.push_back( XMFLOAT3( i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f ) );
	const PhysicsWorld::ShapeId shape = physics.createHullShape( cube );
	REQUIRE( shape != 0 );
	const PhysicsWorld::BodyId plain = physics.addStaticBody( shape, XMMatrixScaling( 2.0f, 2.0f, 2.0f ) * XMMatrixTranslation( 50.0f, 10.0f, 50.0f ) );
	const PhysicsWorld::BodyId turned = physics.addStaticBody( shape, XMMatrixScaling( 2.0f, 2.0f, 2.0f ) * XMMatrixRotationY( XM_PIDIV4 ) *
																	  XMMatrixTranslation( 90.0f, 10.0f, 50.0f ) );
	REQUIRE( plain != 0 );
	REQUIRE( turned != 0 );
	auto top = [&]( float x, float z, PhysicsWorld::BodyId& body )
	{
		float fraction = 0.0f;
		body = 0;
		return physics.castRay( XMFLOAT3( x, 100.0f, z ), XMFLOAT3( 0.0f, -200.0f, 0.0f ), fraction, &body ) ? 100.0f - 200.0f * fraction : -1e9f;
	};
	PhysicsWorld::BodyId body = 0;
	CHECK( top( 50.0f, 50.0f, body ) == doctest::Approx( 11.0f ).epsilon( 0.001 ) );	// верх — на половине масштабированного куба
	CHECK( body == plain );
	CHECK( top( 51.3f, 50.0f, body ) < 0.0f );											// за гранью куба 2 м — мимо
	CHECK( top( 91.3f, 50.0f, body ) == doctest::Approx( 11.0f ).epsilon( 0.001 ) );	// повёрнутый ромбом — дальше по оси
	CHECK( body == turned );
}

TEST_CASE( "PhysicsWorld: a capsule with a non-uniform scale still gets a body" )
{
	PhysicsWorld physics;
	const PhysicsWorld::ShapeId trunk = physics.createCapsuleShape( XMFLOAT3( 0.0f, 0.0f, 0.0f ), 4.0f, 0.5f );
	REQUIRE( trunk != 0 );
	CHECK( physics.addStaticBody( trunk, XMMatrixScaling( 2.0f, 1.0f, 1.0f ) * XMMatrixTranslation( 10.0f, 0.0f, 10.0f ) ) != 0 );
	CHECK( physics.addStaticBody( trunk, XMMatrixScaling( 1.5f, 1.5f, 1.5f ) * XMMatrixTranslation( 20.0f, 0.0f, 10.0f ) ) != 0 );
	CHECK( physics.bodyCount() == 2 );
}

TEST_CASE( "PhysicsWorld: a ray tells the body it hit, the ground is body 0" )
{
	FakeTerrain terrain( flat );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );
	const PhysicsWorld::BodyId box = physics.addStaticBox( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ),
														   XMMatrixTranslation( 128.0f, flatHeight + 1.0f, 128.0f ) );
	float fraction = 0.0f;
	PhysicsWorld::BodyId body = 99;
	REQUIRE( physics.castRay( XMFLOAT3( 128.0f, 50.0f, 128.0f ), XMFLOAT3( 0.0f, -100.0f, 0.0f ), fraction, &body ) );
	CHECK( body == box );
	REQUIRE( physics.castRay( XMFLOAT3( 140.0f, 50.0f, 128.0f ), XMFLOAT3( 0.0f, -100.0f, 0.0f ), fraction, &body ) );
	CHECK( body == 0 );
	CHECK( 50.0f - 100.0f * fraction == doctest::Approx( flatHeight ).epsilon( 0.001 ) );
}

TEST_CASE( "PhysicsWorld: debug lines show bodies and characters nearby, not the ground or far bodies" )
{
	FakeTerrain terrain( flat );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );
	const XMFLOAT3 center( 128.0f, flatHeight, 128.0f );
	std::vector<PhysicsWorld::DebugLine> lines;
	physics.debugLines( center, 40.0f, lines );
	CHECK( lines.empty() );		// только земля

	physics.addStaticBox( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ), XMMatrixTranslation( 130.0f, flatHeight + 1.0f, 128.0f ) );
	physics.addStaticBox( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ), XMMatrixTranslation( 228.0f, flatHeight + 1.0f, 128.0f ) );
	physics.debugLines( center, 40.0f, lines );
	REQUIRE( !lines.empty() );
	bool nearBoxOnly = true;
	for( const PhysicsWorld::DebugLine& line : lines )
		nearBoxOnly &= !line.character && std::abs( line.from.x - 130.0f ) < 1.2f && std::abs( line.to.x - 130.0f ) < 1.2f;
	CHECK( nearBoxOnly );

	lines.clear();
	physics.createCharacter( {}, XMFLOAT3( 126.0f, flatHeight, 128.0f ) );
	physics.debugLines( center, 40.0f, lines );
	CHECK( std::any_of( lines.begin(), lines.end(), []( const PhysicsWorld::DebugLine& line ) { return line.character; } ) );
}

TEST_CASE( "PhysicsWorld: a row of trunks with one shared capsule is a wall" )
{
	FakeTerrain terrain( flat );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );
	// 50 стволов радиусом 0,3 м через метр поперёк пути: просвет 0,4 м уже персонажа (0,6 м)
	const PhysicsWorld::ShapeId trunk = physics.createCapsuleShape( XMFLOAT3( 0.0f, 0.0f, 0.0f ), 8.0f, 0.3f );
	for( int i = 0; i < 50; ++i )
		physics.addStaticBody( trunk, XMMatrixTranslation( 103.0f + i, flatHeight, 135.0f ) );
	physics.optimize();
	CHECK( physics.bodyCount() == 50 );
	const PhysicsWorld::CharacterId character = physics.createCharacter( {}, XMFLOAT3( 128.4f, flatHeight, 128.0f ) );
	const PhysicsWorld::CharacterState state = walk( physics, character, XMFLOAT2( 0.0f, 1.5f ), 10.0f );
	CHECK( state.feet.z < 135.0f - 0.3f );
	CHECK( state.feet.z > 133.5f );
}

TEST_CASE( "PhysicsWorld: a character stops at the map edge, a ray goes through the boundary" )
{
	FakeTerrain terrain( flat );
	PhysicsWorld physics;
	REQUIRE( physics.buildTerrain( terrain ) );
	CHECK( physics.bodyCount() == 0 );		// граница — не тела моделей
	// К краю x = 0 и дальше: за краем земли нет — стена границы держит у края, на земле
	const PhysicsWorld::CharacterId character = physics.createCharacter( {}, XMFLOAT3( 3.0f, flatHeight, 128.0f ) );
	const PhysicsWorld::CharacterState state = walk( physics, character, XMFLOAT2( -2.0f, 0.0f ), 4.0f );
	CHECK( state.feet.x > 0.0f );
	CHECK( state.feet.x < 1.0f );
	CHECK( std::abs( state.feet.y - flatHeight ) < 0.05f );
	CHECK( state.onGround );
	// Луч снаружи карты внутрь — сквозь стену в коробку за ней (передняя грань — x = 4)
	const PhysicsWorld::BodyId box = physics.addStaticBox( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ),
														   XMMatrixTranslation( 5.0f, flatHeight + 1.0f, 128.0f ) );
	float fraction = 0.0f;
	PhysicsWorld::BodyId body = 0;
	REQUIRE( physics.castRay( XMFLOAT3( -20.0f, flatHeight + 1.0f, 128.0f ), XMFLOAT3( 40.0f, 0.0f, 0.0f ), fraction, &body ) );
	CHECK( body == box );
	CHECK( -20.0f + 40.0f * fraction == doctest::Approx( 4.0f ).epsilon( 0.01 ) );
}
