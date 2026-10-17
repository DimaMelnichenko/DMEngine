#include "World.h"
#include "Components.h"
#include <algorithm>
#include <cmath>

namespace GS
{

void World::addSystem( std::unique_ptr<SimulationSystem> system )
{
	m_systems.push_back( std::move( system ) );
}

void World::setStepRate( float rate )
{
	m_stepRate = std::clamp( rate, 10.0f, 240.0f );
}

void World::setPaused( bool paused )
{
	m_paused = paused;
	// После паузы — с целого шага: накопленное до неё не догоняется
	if( paused )
		m_accumulator = 0.0;
}

uint32_t World::tick( double seconds )
{
	m_clock.advance( seconds );
	m_lastSteps = 0;
	if( m_paused || !m_clock.gameTimeRunning() || seconds <= 0.0 )
		return 0;

	const double stepSeconds = 1.0 / m_stepRate;
	// Запас на округление: кадр ровно в N шагов (0,05 с при 60 Гц) — N шагов, а не N − 1 с остатком почти в шаг
	constexpr double epsilon = 1e-9;
	m_accumulator += seconds;
	while( m_accumulator + epsilon >= stepSeconds && m_lastSteps < maxStepsPerFrame )
	{
		step( static_cast<float>( stepSeconds ) );
		m_accumulator = std::max( m_accumulator - stepSeconds, 0.0 );
		++m_lastSteps;
	}
	// Долгий кадр: не догонять — остаётся доля шага, сущности просто медленнее на этом кадре
	if( m_accumulator + epsilon >= stepSeconds )
	{
		m_accumulator = std::fmod( m_accumulator, stepSeconds );
		if( m_accumulator + epsilon >= stepSeconds )
			m_accumulator = 0.0;
	}
	return m_lastSteps;
}

void World::step( float seconds )
{
	// Прежнее место — для смешения при отрисовке
	for( auto [entity, transform, previous] : m_registry.view<const Transform, PreviousTransform>().each() )
		previous.transform = transform;
	for( const auto& system : m_systems )
		system->step( *this, seconds );
	++m_steps;
}

float World::interpolation() const
{
	return static_cast<float>( std::clamp( m_accumulator * m_stepRate, 0.0, 1.0 ) );
}

size_t World::entityCount() const
{
	return m_registry.view<const Transform>().size();
}

void World::clear()
{
	m_registry.clear();
}

}
