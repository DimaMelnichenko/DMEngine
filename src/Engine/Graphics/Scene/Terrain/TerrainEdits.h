#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "Level\LevelSettings.h"

namespace GS
{

// Карта высот, на которую накладываются правки: нормированные высоты 0…1 (высота, м = значение × heightMultiplier +
// heightOffset) квадратом size × size, строки по rowPitch чисел; строка 0 — дальний край по z (v = 1 − z / worldSize,
// как у CDLODTerrain и Shaders/terrain_height.sh), тексель — texelSize метров
struct HeightField
{
	float* heights = nullptr;
	uint32_t size = 0;
	size_t rowPitch = 0;
	float texelSize = 1.0f;
	float heightMultiplier = 1.0f;
	float heightOffset = 0.0f;
};

// Правки рельефа по порядку слоёв (как Edit Layers и Landscape Splines в UE): кривая через точки правки — Catmull-Rom
// (или ломаная) с высотой, шириной плоской части и полосой перехода, интерполированными вдоль неё; каждый тексель берёт
// ближайшую точку кривой, в пределах половины ширины рельеф идёт к её высоте целиком, в полосе перехода — плавно
// (косинусом) к исходному. raise / lower — можно ли поднимать и опускать. Одна точка — круглая площадка. Высоты
// остаются в 0…1: правка выше или ниже диапазона карты срезается. Возвращает число изменённых текселей
size_t applyTerrainEdits( HeightField& field, const std::vector<TerrainEdit>& edits );

}
