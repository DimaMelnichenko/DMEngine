#pragma once

#include <array>
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

// Что правки делают, кроме высоты (Landscape Splines в UE: Paint Layer и очистка растительности): поля того же размера
// и раскладки строк, что у HeightField (строки подряд, без rowPitch). Пустое поле — правки его не меняют
struct TerrainEditCoverage
{
	static constexpr uint32_t paintLayers = 8;	// TerrainMaterial::maxLayers

	uint32_t size = 0;
	// 0…1: доля растительности, которую убирают правки (наибольшая из них)
	std::vector<float> foliageClear;
	// Покраска слоёв по порядку правок: итоговый вес слоя i = исходный вес × remaining + paint[i] (правка с весом w
	// умножает и то и другое на 1 − w и добавляет w своему слою — как кисть поверх прежних)
	std::vector<float> remaining;
	std::array<std::vector<float>, paintLayers> paint;

	bool clearsFoliage() const { return !foliageClear.empty(); }
	bool paints() const { return !remaining.empty(); }
	// Билинейно по центрам текселей, u, v — доли стороны карты (v = 0 — строка 0)
	float sample( const std::vector<float>& values, float u, float v ) const;
};

// Правки рельефа по порядку слоёв (как Edit Layers и Landscape Splines в UE): кривая через точки правки — Catmull-Rom
// (или ломаная) с высотой, шириной плоской части и полосой перехода, интерполированными вдоль неё; каждый тексель берёт
// ближайшую точку кривой, в пределах половины ширины рельеф идёт к её высоте целиком, в полосе перехода — плавно
// (косинусом) к исходному. raise / lower — можно ли поднимать и опускать. Одна точка — круглая площадка. Высоты
// остаются в 0…1: правка выше или ниже диапазона карты срезается. С тем же весом правка убирает растительность и красит
// слой в coverage (если задан). Возвращает число изменённых текселей высоты
size_t applyTerrainEdits( HeightField& field, const std::vector<TerrainEdit>& edits, TerrainEditCoverage* coverage = nullptr );

}
