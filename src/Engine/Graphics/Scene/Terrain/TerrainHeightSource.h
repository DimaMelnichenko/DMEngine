#pragma once

#include <string>

namespace GS
{

// Высота рельефа для тех, кто на нём стоит (расстановка травы и декора, частицы), без привязки к устройству
// террейна. Карта высот покрывает квадрат [0, worldSize] по X и Z, ось Z идёт по текстуре снизу вверх
// (v = 1 - z / worldSize), высота = значение карты * heightMultipler + heightOffset. Формулы для шейдеров —
// Shaders\terrain_height.sh
struct TerrainHeight
{
	std::string heightMap;	// имя в хранилище текстур
	float worldSize = 0.0f;
	float heightMultipler = 1.0f;
	float heightOffset = 0.0f;
};

class TerrainHeightSource
{
public:
	virtual ~TerrainHeightSource() = default;
	// Текущие параметры: множитель высоты можно менять во время работы
	virtual TerrainHeight terrainHeight() const = 0;
};

}
