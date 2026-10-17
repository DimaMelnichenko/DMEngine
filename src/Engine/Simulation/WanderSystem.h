#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include "World.h"

namespace GS
{

// Блуждание в круге — рулевые поведения wander и containment (C. Reynolds, «Steering Behaviors For Autonomous
// Characters», 1999): впереди сущности окружность, точка на ней понемногу сдвигается случайно — направление меняется
// плавно; у края круга сущность разворачивается к его центру, упёршись — отворачивает. Поворот — не быстрее turnRate.
// Решает только, куда идти: скорость и взгляд — в Locomotion, двигает MovementSystem (по земле или персонажем мира
// столкновений). Общий кирпич поведения существ: зверь пасётся, бродя по поляне
struct Wander
{
	DirectX::XMFLOAT2 center = DirectX::XMFLOAT2( 0.0f, 0.0f );	// центр круга на земле (x, z), м
	float radius = 10.0f;		// м
	float speed = 1.0f;			// м/с
	float turnRate = 1.5f;		// наибольшая скорость поворота, рад/с
	float heading = 0.0f;		// направление хода, рад: 0 — +Z, π/2 — +X
	float wanderAngle = 0.0f;	// точка блуждания на окружности впереди, рад от направления хода
	uint32_t random = 1;		// состояние генератора (xorshift32): у каждой сущности своё — путь повторяется
	bool moving = false;		// уже шла (до первого шага упереться не во что)
};

class WanderSystem : public SimulationSystem
{
public:
	// Окружность блуждания: на circleDistance впереди, радиус circleRadius (м); точка на ней сдвигается до
	// ±jitter рад/с
	static constexpr float circleDistance = 2.0f;
	static constexpr float circleRadius = 1.0f;
	static constexpr float jitter = 3.0f;

	const char* name() const override { return "Wander"; }
	void step( World& world, float seconds ) override;
};

}
