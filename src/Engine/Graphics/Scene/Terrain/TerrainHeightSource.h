#pragma once

#include <cstdint>
#include "DirectX.h"

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
	float maxValue = 1.0f;		// наибольшее значение итоговой карты (нормированное, как текстура)
	// Номер итоговой карты: растёт, когда её строят заново (загрузка, перестройка русел) — тем, кто считает по ней на GPU
	// (тень рельефа, TerrainSunShadow)
	uint32_t revision = 0;
	// Детальная земля у русел — ресурс сцены (SLOT_TERRAIN_DETAIL, terrain_detail.sh): сторона плитки, м; 0 — её нет
	float detailTileSize = 0.0f;
	// Массив плиток и индекс (те же, что в слотах сцены) — тем, кто считает по ним при загрузке, до первого compute
	// террейна (детальная вода у русел, WaterSimulation)
	const ShaderView* detailTiles = nullptr;
	const ShaderView* detailIndex = nullptr;
};

// Горы вокруг карты (FarTerrain) — тем, кто считает по рельефу и за её краем (тень рельефа, TerrainSunShadow)
struct FarTerrainHeight
{
	const ShaderView* heightMap = nullptr;	// карта гор: высоты в метрах (R32_FLOAT, мипы), строки с севера; nullptr — гор нет
	DirectX::XMFLOAT2 origin = DirectX::XMFLOAT2( 0.0f, 0.0f );	// угол с наименьшими x, z, м
	float extent = 0.0f;		// сторона, м
	uint32_t size = 0;			// текселей по стороне
	float maxHeight = 0.0f;		// наибольшая высота, м
	uint32_t revision = 0;		// растёт с каждым запеканием (новая итоговая карта уровня)
	uint32_t shadowSize = 0;	// текселей тени гор по стороне («Shadow resolution»)
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
	// Итоговая карта высот на CPU — нормированная, как текстура (строки сверху вниз, mapSize²); nullptr — её нет. Для
	// отпечатка того, что посчитано по рельефу (кэш течения воды, WaterSimulation)
	virtual const float* cpuHeights() const { return nullptr; }
};

}
