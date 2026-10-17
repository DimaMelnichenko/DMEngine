#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <entt/entity/registry.hpp>
#include "WorldClock.h"
#include "PhysicsWorld.h"

namespace GS
{

class TerrainHeightSource;
class World;

// Система мира — поведение над компонентами сущностей (как processor в Mass UE, system в ECS): шаг фиксированной
// длительности, данные — из реестра и служб мира
class SimulationSystem
{
public:
	virtual ~SimulationSystem() = default;
	virtual const char* name() const = 0;
	virtual void step( World& world, float seconds ) = 0;
};

// Мир симуляции (как World с Mass в UE, на EnTT): сущности с компонентами (Simulation/Components.h), системы и мировые
// часы. Шаг фиксированный («Fix Your Timestep», Gaffer on Games): кадр копит реальное время, системы шагают по
// 1 / stepRate, пока оно есть, не больше maxStepsPerFrame за кадр (остаток сверх — отбрасывается: долгий кадр не тянет за
// собой ещё более долгий); до шага Transform сущностей — в PreviousTransform, отрисовка смешивает их по interpolation().
// Службы мира — рельеф (указатель, его ставит владелец, Scene) и столкновения (PhysicsWorld, свои): системы получают мир, а
// не синглтоны. Ядро без устройства: рисует сущности движок (WorldEntities)
class World
{
public:
	static constexpr float defaultStepRate = 60.0f;
	static constexpr uint32_t maxStepsPerFrame = 4;

	World();
	~World();
	World( const World& ) = delete;
	World& operator=( const World& ) = delete;

	entt::registry& registry() { return m_registry; }
	const entt::registry& registry() const { return m_registry; }
	WorldClock& clock() { return m_clock; }
	const WorldClock& clock() const { return m_clock; }

	// Рельеф: высота земли (TerrainHeightSource::surfaceHeight); nullptr — земли нет. Землю мира столкновений строит
	// владелец (PhysicsWorld::buildTerrain), новую итоговую карту (TerrainHeight::revision) мир перестраивает сам в tick
	void setTerrain( const TerrainHeightSource* terrain ) { m_terrain = terrain; }
	const TerrainHeightSource* terrain() const { return m_terrain; }
	// Столкновения: земля, неподвижные тела, персонажи
	PhysicsWorld& physics() { return m_physics; }
	const PhysicsWorld& physics() const { return m_physics; }

	// Системы шагают в порядке добавления
	void addSystem( std::unique_ptr<SimulationSystem> system );

	// Кадр: часы — на seconds реального времени, затем шаги систем; число шагов этого кадра. Стоят шаги на паузе мира и
	// когда стоит время игры (WorldClock::gameTimeRunning, -freezetime)
	uint32_t tick( double seconds );
	// Частота шагов, Гц (10…240)
	void setStepRate( float rate );
	float stepRate() const { return m_stepRate; }
	void setPaused( bool paused );
	bool paused() const { return m_paused; }
	// Доля шага, накопленная после последнего (0…1): смешение PreviousTransform → Transform при отрисовке
	float interpolation() const;
	uint32_t lastSteps() const { return m_lastSteps; }
	uint64_t steps() const { return m_steps; }
	// Сущности с местом (Transform)
	size_t entityCount() const;
	// Убрать все сущности
	void clear();

private:
	void step( float seconds );
	// Сущность с персонажем убрана — персонажа тоже (сигнал реестра)
	void removeCharacter( entt::registry& registry, entt::entity entity );

	// Столкновения — до реестра: уничтожаются после него (персонажи сущностей убираются, пока мир столкновений есть)
	PhysicsWorld m_physics;
	entt::registry m_registry;
	WorldClock m_clock;
	const TerrainHeightSource* m_terrain = nullptr;
	std::vector<std::unique_ptr<SimulationSystem>> m_systems;
	float m_stepRate = defaultStepRate;
	double m_accumulator = 0.0;		// реальное время, ещё не прошедшее шагами, с
	bool m_paused = false;
	uint32_t m_lastSteps = 0;
	uint64_t m_steps = 0;
};

}
