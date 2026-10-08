#pragma once

#include <cstdint>

class ShaderView;

namespace GS
{

// Высота рельефа для тех, кто на нём стоит (расстановка травы и декора, частицы), без привязки к устройству
// террейна. Карта высот покрывает квадрат [0, worldSize] по X и Z, ось Z идёт по текстуре снизу вверх
// (v = 1 - z / worldSize), высота = значение карты * heightMultiplier + heightOffset. Формулы для шейдеров —
// Shaders\terrain_height.sh
struct TerrainHeight
{
	const ShaderView* heightMap = nullptr;	// итоговая карта — с правками рельефа (R32_FLOAT, мипы)
	// Доля растительности, которую убрали правки рельефа (R8_UNORM, те же UV; русло, площадка): 0 — не тронута
	const ShaderView* foliageClearMask = nullptr;
	float worldSize = 0.0f;
	uint32_t mapSize = 0;	// текселей карты высот по стороне
	float heightMultiplier = 1.0f;
	float heightOffset = 0.0f;
};

class TerrainHeightSource
{
public:
	virtual ~TerrainHeightSource() = default;
	// Текущие параметры: множитель высоты можно менять во время работы
	virtual TerrainHeight terrainHeight() const = 0;
	// Высота поверхности в точке (x, z) мира, как её рисует террейн вблизи — треугольники сетки LOD 0 по итоговой карте, м;
	// false — точка вне карты или высот на CPU нет. Для того, кто ходит по земле (WalkMode)
	virtual bool surfaceHeight( float x, float z, float& height ) const { return false; }
};

}
