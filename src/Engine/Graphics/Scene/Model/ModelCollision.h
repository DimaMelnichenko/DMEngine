#pragma once

#include <unordered_map>
#include <DirectXMath.h>
#include "Engine\Simulation\PhysicsWorld.h"

namespace GS
{

class DMModel;

// Простая коллизия моделей в мире столкновений (DMModel::collision, Models.collision; docs/physics.md) по LOD 0: оболочка
// вершин всех секций, треугольники всех секций или ствол — вертикальная капсула на всю высоту модели, ось — по вершинам у
// общего низа модели (TrunkCollision.h), радиус — Models.collision_radius или их полуширина — общая форма раз на модель (PhysicsWorld::create…Shape), её
// берут экземпляры уровня, лес и камни расстановки с масштабом тела; коробка по границам — своя у тела (дешёвая, масштаб
// запечён: плоская модель остаётся тонкой)
class ModelCollision
{
public:
	explicit ModelCollision( PhysicsWorld& physics ) : m_physics( physics ) {}

	// Тело модели с мировой матрицей world; 0 — без столкновений (none, у модели нет LOD)
	PhysicsWorld::BodyId addBody( const DMModel& model, DirectX::FXMMATRIX world );
	PhysicsWorld& physics() { return m_physics; }

private:
	// Общая форма модели (не коробка): строится при первом теле
	PhysicsWorld::ShapeId shape( const DMModel& model );

	PhysicsWorld& m_physics;
	std::unordered_map<const DMModel*, PhysicsWorld::ShapeId> m_shapes;
};

}
