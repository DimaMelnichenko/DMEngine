#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "DirectX.h"
#include "Level\LevelSettings.h"
#include "TerrainEdits.h"

namespace GS
{

// Русла и вода по итоговому рельефу — ступень конвейера рельефа и воды (docs/water.md, «Конвейер»): карта высот →
// ручные правки (TerrainEdits) → сток, русла, ручьи и маска озёр → вода (WaterSimulation). Каждая ступень видит итог
// предыдущих, поэтому ручей идёт по каналу, который прорезала правка, а чаша правки наливается озером. Как Edit Layers и
// Water в UE: вода выводится из готового рельефа при загрузке, а не хранится отдельно.
//
// Шаги (на CPU, в double; прежде — офлайн-сценарий Tools/carve_channels.py, формулы и порядок обхода — те же):
// 1. Priority-Flood с малым уклоном (Barnes, Lehman, Mulla 2014) заполняет низины, сток D8 по заполненной карте
//    (O'Callaghan, Mark 1984), водосбор — площадь выше по течению;
// 2. приток — доля «русла» по водосбору (flowStart … flowFull в логарифме, sourceRate) и источники-помощники
//    (WaterSources), расход — сумма притока выше по течению;
// 3. ось русла — ячейки с расходом больше minDischarge, сглажена вдоль главного притока, извилины на пологом;
// 4. ложе горного ручья: дно шириной a·Q^0.5, врез c·Q^0.4, тальвег, борта; дно монотонно вниз по течению; в озёрах
//    (низинах глубже lakeDepth) не режет;
// 5. озёра — связные низины глубже lakeDepth и не меньше minLakeArea с притоком внутри, уровень — перелива (ровный); ямка
//    меньше — часть русла; уровень воды ручья по Маннингу в точках оси, ручьи — цепочки от истока до слияния, озера или
//    края; растр статичной воды (уровень ручья, течение, уровень озера) и водосбор вдоль русла (приток в режиме simulated)
class TerrainHydrology
{
public:
	struct Result
	{
		uint32_t size = 0;								// клеток по стороне — как у карты высот
		// Опускание ложа, м (≤ 0), строки как у карты высот (строка 0 — дальний край по z): растровая правка рельефа
		std::vector<float> lowering;
		// R — уровень воды ручья, м (−1e9 — нет), G, B — течение по X и Z мира, м/с, A — уровень озера, м (−1e9 — не озеро);
		// Shaders/water_simulation.cs, mainStatic
		std::vector<DirectX::XMFLOAT4> staticWater;
		std::vector<float> channelFlow;					// водосбор вдоль оси русла, м²: приток симуляции (mainSources)
		std::vector<WaterStream> streams;				// ленты ручьёв (StreamRibbons)

		// Сводка для лога
		size_t nodes = 0;
		size_t carvedCells = 0;
		size_t points = 0;
		float deepestLowering = 0.0f;
		float largestDischarge = 0.0f;
	};

	// field — итоговая карта высот с ручными правками; water — строка WaterSimulation: приток, помощники, русла
	static bool build( const HeightField& field, const WaterSimulationSettings& water, Result& result );
	// Растровая правка рельефа по опусканию: опускает, красит дно галькой и убирает растительность (как правка
	// channels прежнего сценария); имя — для лога
	static TerrainEdit loweringEdit( const Result& result, const WaterChannelsSettings& channels );
};

}
