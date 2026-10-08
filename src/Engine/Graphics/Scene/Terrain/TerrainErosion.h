#pragma once

#include <cstdint>
#include <vector>
#include "Level\LevelSettings.h"

namespace GS
{

// Эрозия карты высот на GPU — первая ступень конвейера рельефа и воды (docs/terrain.md, «Эрозия»): исходная карта из любого
// источника (Tools/gen_heightmap.py, World Machine, Gaea) → эрозия → ручные правки (TerrainEdits) → русла и вода
// (TerrainHydrology). Как Erosion и Thermal в World Machine и Gaea:
// 1. капли воды размывают склоны и откладывают грунт (H. T. Beyer, «Implementation of a method for hydraulic erosion»,
//    2015, как у S. Lague): пакетами по 65 536, шаг за шагом; изменения шага — целочисленными атомиками, поэтому
//    результат одинаков от запуска к запуску;
// 2. осыпание склонов круче угла откоса (Musgrave, Kolb, Mace 1989; Olsen 2004).
// Шейдер — Shaders/terrain_erosion.cs. Результат CDLODTerrain кэширует на диске (каталог eroded\ рядом с картой высот) —
// повторная загрузка без GPU
class TerrainErosion
{
public:
	// Высоты, м, квадратом size × size, строки — как у карты высот (строка 0 — дальний край по z)
	struct Result
	{
		std::vector<float> height;		// после эрозии
		std::vector<float> wear;		// размыв — понижение против исходной, м
		std::vector<float> deposition;	// отложения — повышение против исходной, м
		std::vector<float> talus;		// осыпь — сколько грунта пришло в клетку, м
	};

	// heights — исходная карта, м; cell — шаг сетки, м. Ждёт GPU. false — ресурсы не созданы или шейдер не собран
	static bool run( const std::vector<float>& heights, uint32_t size, float cell, const TerrainErosionSettings& settings,
					 Result& result );
};

}
