#pragma once

#include <cstdint>
#include <vector>
#include <DirectXMath.h>

namespace GS
{

class DMModel;

// Треугольники LOD модели на CPU — все секции подряд (позиция — первое поле вершины любого формата, VertexData): форма
// коллизии (ModelCollision), верх камней русла для воды (Scatterer, StoneTop). false — LOD нет или у секций нет мешей
bool modelLodGeometry( const DMModel& model, uint16_t lod, std::vector<DirectX::XMFLOAT3>& points, std::vector<uint32_t>& indices );

}
