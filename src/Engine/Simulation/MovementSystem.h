#pragma once

#include "World.h"

namespace GS
{

// Передвижение сущностей (как Character Movement в UE): желаемая скорость и взгляд — из Locomotion (их пишет поведение),
// место — в Transform. Сущность с персонажем (Character) идёт запросами к миру столкновений (PhysicsWorld::moveCharacter:
// капсула, ступеньки, уклон, глубокая вода, другие персонажи, гравитация и падение); без него — прямо по земле мира
// (surfaceHeight), сквозь всё. Взгляд — поворот вокруг вертикали по Locomotion::heading
class MovementSystem : public SimulationSystem
{
public:
	static constexpr float gravity = 9.81f;		// м/с²

	const char* name() const override { return "Movement"; }
	void step( World& world, float seconds ) override;
};

}
