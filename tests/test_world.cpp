// Мир симуляции (World): кадр — фиксированными шагами с остатком, предел шагов за кадр, прежнее место до шага и
// смешение мест, пауза мира и остановленное время игры
#include <doctest/doctest.h>
#include <memory>
#include "Engine\Simulation\World.h"
#include "Engine\Simulation\Components.h"

using namespace GS;

namespace
{

// Шаги считает, сущности с местом двигает на метр по X за шаг
class CountingSystem : public SimulationSystem
{
public:
	const char* name() const override { return "Counting"; }
	void step( World& world, float seconds ) override
	{
		++steps;
		lastSeconds = seconds;
		for( auto [entity, transform] : world.registry().view<Transform>().each() )
			transform.position.x += 1.0f;
	}

	uint32_t steps = 0;
	float lastSeconds = 0.0f;
};

CountingSystem& addCounter( World& world )
{
	auto system = std::make_unique<CountingSystem>();
	CountingSystem& counter = *system;
	world.addSystem( std::move( system ) );
	return counter;
}

}

TEST_CASE( "World: a frame is split into fixed steps, the rest carries over" )
{
	World world;
	CountingSystem& counter = addCounter( world );
	CHECK( world.tick( 0.05 ) == 3 );			// 3 шага по 1/60 с
	CHECK( counter.steps == 3 );
	CHECK( counter.lastSeconds == doctest::Approx( 1.0f / 60.0f ) );
	CHECK( world.interpolation() == doctest::Approx( 0.0f ).epsilon( 1e-4 ) );
	CHECK( world.tick( 0.01 ) == 0 );			// мелкий кадр копится
	CHECK( world.interpolation() == doctest::Approx( 0.6f ).epsilon( 1e-3 ) );
	CHECK( world.tick( 0.01 ) == 1 );
	CHECK( world.interpolation() == doctest::Approx( 0.2f ).epsilon( 1e-3 ) );
	CHECK( world.steps() == 4 );
}

TEST_CASE( "World: a long frame makes at most maxStepsPerFrame steps and drops the rest" )
{
	World world;
	CountingSystem& counter = addCounter( world );
	CHECK( world.tick( 1.0 ) == World::maxStepsPerFrame );
	CHECK( counter.steps == World::maxStepsPerFrame );
	CHECK( world.interpolation() < 1.0f );
	CHECK( world.tick( 1.0 / 60.0 ) <= 2 );		// следующий кадр не догоняет отброшенное
}

TEST_CASE( "World: step rate sets the step length" )
{
	World world;
	CountingSystem& counter = addCounter( world );
	world.setStepRate( 30.0f );
	CHECK( world.tick( 0.1 ) == 3 );
	CHECK( counter.lastSeconds == doctest::Approx( 1.0f / 30.0f ) );
}

TEST_CASE( "World: the place before the step is kept for interpolation" )
{
	World world;
	addCounter( world );
	entt::registry& registry = world.registry();
	const entt::entity entity = registry.create();
	registry.emplace<Transform>( entity );
	registry.emplace<PreviousTransform>( entity );
	world.tick( 1.0 / 60.0 );
	CHECK( registry.get<PreviousTransform>( entity ).transform.position.x == 0.0f );
	CHECK( registry.get<Transform>( entity ).position.x == 1.0f );
	world.tick( 1.0 / 60.0 );
	CHECK( registry.get<PreviousTransform>( entity ).transform.position.x == 1.0f );
	CHECK( registry.get<Transform>( entity ).position.x == 2.0f );
	// Отрисовка между шагами — посередине
	const Transform middle = interpolate( registry.get<PreviousTransform>( entity ).transform, registry.get<Transform>( entity ), 0.5f );
	CHECK( middle.position.x == doctest::Approx( 1.5f ) );
	CHECK( world.entityCount() == 1 );
	world.clear();
	CHECK( world.entityCount() == 0 );
}

TEST_CASE( "World: a paused world and frozen game time make no steps, the clock goes on" )
{
	World world;
	CountingSystem& counter = addCounter( world );
	world.clock().setHours( 12.0 );
	world.clock().setTimeScale( 3600.0f );
	world.setPaused( true );
	CHECK( world.tick( 0.1 ) == 0 );
	CHECK( world.clock().hours() == doctest::Approx( 12.1 ) );
	CHECK( world.clock().gameSeconds() == doctest::Approx( 0.1 ) );	// пауза мира — не пауза времени игры
	world.setPaused( false );
	world.clock().setGameTimeRunning( false );
	CHECK( world.tick( 0.1 ) == 0 );
	CHECK( counter.steps == 0 );
	CHECK( world.clock().gameSeconds() == doctest::Approx( 0.1 ) );
}
