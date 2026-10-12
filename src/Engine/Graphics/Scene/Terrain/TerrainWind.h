#pragma once

#include <string>
#include <vector>
#include "DirectX.h"
#include "Level\LevelSettings.h"
#include "TerrainFields.h"

namespace GS
{

// Препятствие ветру — экземпляр модели уровня выше нескольких метров (ель рощи, постройка): круг по габариту
struct WindObstacle
{
	float x;		// центр, м мира
	float z;
	float radius;	// м
};

// Поле ветра по рельефу — ступень конвейера рельефа и воды (docs/wind.md, «Поле ветра по рельефу»), упрощённый WindNinja
// (US Forest Service): ветер уровня — над ровным открытым местом, а в каждой точке карты — свой, в долях ветра уровня.
// 1. Рельеф — массово-согласованный поток (Sherman 1978, WindNinja mass-consistent): слой воздуха над рельефом, верх —
//    рельеф, сглаженный на layerSmoothing, плюс layerHeight; ровный ветер уровня поправляется давлением до ∇·(h u) = 0
//    (h — толщина слоя): где слой тоньше (гребень, перевал, скала у прохода), ветер быстрее, поток обтекает высокое.
//    Давление — красно-чёрный Гаусс-Зейдель с верхней релаксацией на разнесённой сетке (как течение воды,
//    Shaders/water_flow.cs), каскадом грубая → тонкая сетка: решение грубой — начальное приближение тонкой.
// 2. Подветренная тень — угол горизонта навстречу ветру Sx (upwindHorizon): за гребнем потенциальный поток ускорялся
//    бы, как перед ним, — тень его тормозит и делает порывистым.
// 3. Вертикаль — течение вдоль рельефа: вверх по наветренному склону, вниз по подветренному.
// 4. Полог (лес, постройки): скорость тянется вдоль ветра к 1 − forestDamping · полог — в лес быстро, из леса медленно:
//    опушка — стена, поляна в лесу тихая, над полем и озером ветер разгоняется до скорости рельефа.
// Всё на CPU параллельно по строкам; CDLODTerrain кэширует результат и держит текстуру для шейдеров (SLOT_WIND_FIELD)
class TerrainWind
{
public:
	// Тексель поля: x, z — скорость по X и Z мира в долях скорости ветра уровня (направление × множитель), y — вертикальная
	// (вдоль рельефа: + вверх по склону), w — порывистость 0…1
	using Cell = DirectX::XMFLOAT4;

	// Шаги 1–3 по рельефу: heights — м, size × size, texel — шаг, м; windX, windZ — куда дует ветер уровня. false — ветра нет
	static bool solveTerrain( const TerrainField& heights, uint32_t size, float texel, float windX, float windZ,
							  const WindFieldSettings& settings, std::vector<Cell>& result );
	// Полог 0…1: маски леса (сумма; редкие деревья — почти не полог, переход от 0,15 до 0,6) и препятствия кругом по
	// габариту; worldSize — сторона карты, м
	static TerrainField canopy( const std::vector<const TerrainField*>& forest, uint32_t size, float worldSize,
								const std::vector<WindObstacle>& obstacles );
	// Шаг 4: скорость рельефа × множитель полога вдоль ветра
	static void applyCanopy( const std::vector<Cell>& terrain, uint32_t size, float texel, const TerrainField& canopy,
							 const WindFieldSettings& settings, std::vector<Cell>& result );
	// Сводка в лог: средний множитель, доля быстрее 1,2 и тише 0,5, наибольший, средняя вертикаль и порывистость
	static std::string summary( const std::vector<Cell>& field );
	// Отпечаток сборки (дата и время компиляции TerrainWind.cpp) — в ключ кэша: правка расчёта меняет его сама
	static const char* codeStamp();
};

}
