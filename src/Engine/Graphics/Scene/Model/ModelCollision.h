#pragma once

#include <DirectXMath.h>
#include "Engine\Simulation\PhysicsWorld.h"

namespace GS
{

class DMModel;

// Тело модели в мире столкновений по её простой коллизии (DMModel::collision, Models.collision; docs/physics.md) — по LOD 0:
// оболочка вершин всех секций, коробка по границам, треугольники всех секций или ствол — вертикальная капсула от низа
// границ на всю высоту модели, радиус — по границам основания (вершины у земли: ствол без кроны). world — мировая матрица
// экземпляра. 0 — без столкновений (none, у модели нет LOD)
PhysicsWorld::BodyId addModelBody( PhysicsWorld& physics, const DMModel& model, DirectX::FXMMATRIX world );

}
