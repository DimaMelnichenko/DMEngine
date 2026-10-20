#pragma once

#include <cstdint>
#include "D3D\GpuResources.h"

// Узор пены воды — «кружево» (FoamLace, docs/water.md, «Пена»): текстура для water_shading.sh (слот 4 поверхности воды)
namespace WaterFoamLace
{

// Бесшовная карта size² (степень двойки) со всеми мипами, R8G8B8A8_UNORM: R — маска пены, G, B — нормаль по ней, A — шум
// растворения (GS::generateFoamLace); мипы — среднее 2 × 2. Одинаковая при каждом запуске
bool create( uint32_t size, Texture& texture, ShaderView& view );

}
