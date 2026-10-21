#pragma once

#include <cstdint>
#include "D3D\GpuResources.h"

// Узор пены воды (FoamLace, docs/water.md, «Пена»): текстура для water_shading.sh (слот 4 поверхности воды)
namespace WaterFoamLace
{

// Бесшовный массив из двух карт size² (степень двойки) со всеми мипами, R8G8B8A8_UNORM: срез 0 — кружево
// (GS::generateFoamLace), срез 1 — густая пена (GS::generateFoamDense); R — маска пены, G, B — нормаль по ней, A — шум
// растворения; мипы — среднее 2 × 2. Одинаковый при каждом запуске
bool create( uint32_t size, Texture& texture, ShaderView& view );

}
