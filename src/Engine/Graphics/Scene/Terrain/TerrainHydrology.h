#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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
// 3. ось русла — ячейки с расходом больше minDischarge, сглажена вдоль главного притока, извилины на пологом — генератор
//    кривых (generate); дальше всё — по кривым из базы (Streams), сгенерированным и правленным (build);
// 4. ложе горного ручья: дно шириной a·Q^0.5, врез c·Q^0.4 (не мельче воды с запасом бровки freeboard), тальвег, борта;
//    дно монотонно вниз по течению; дно озёр — чаша (отмель, свал, глубина от площади); в озёрах
//    (низинах глубже lakeDepth) не режет, ниже озера врез нарастает от порога;
// 5. озёра — связные низины глубже lakeDepth и не меньше minLakeArea с притоком внутри и их мелкая кромка ниже уровня,
//    уровень — перелива (ровный); ямка меньше — часть русла; вода — одно поле по сети: своя вода узла, затем подпор от
//    устьев вверх, озеро — вода ниже по течению у впадающих и уровень истока; уровень воды ручья по Маннингу в точках оси, ручьи — цепочки от истока до слияния, озера или
//    края; растр статичной воды (уровень ручья, течение, уровень озера) и водосбор вдоль русла (приток в режиме simulated)
class TerrainHydrology
{
public:
	// Детальная земля у русел: плитка — detailTileCells клеток карты (лист квадродерева CDLOD), detailSamples точек на
	// клетку (0,25 м при клетке 1 м) и поле в тексель с каждой стороны: (detailTileCells · detailSamples + 2)² точек
	static constexpr uint32_t detailTileCells = 32;
	static constexpr uint32_t detailSamples = 4;
	struct DetailTile
	{
		uint32_t x = 0;					// плитка по x и z мира (z — от ближнего края карты)
		uint32_t z = 0;
		std::vector<float> heights;		// высота, м; строки — по z мира вверх, тексели — по x
		std::vector<float> carve;		// врез русла (или чаши озера), м: покраска галькой и очистка растительности в шейдерах
	};

	struct Result
	{
		uint32_t size = 0;								// клеток по стороне — как у карты высот
		// Опускание ложа, м (≤ 0), строки как у карты высот (строка 0 — дальний край по z): растровая правка рельефа
		std::vector<float> lowering;
		// R — уровень воды ручья, м (−1e9 — нет), G, B — течение по X и Z мира, м/с, A — уровень озера, м (−1e9 — не озеро);
		// Shaders/water_simulation.cs, mainStatic
		std::vector<DirectX::XMFLOAT4> staticWater;
		std::vector<float> channelFlow;					// водосбор вдоль оси русла, м²: приток симуляции (mainSources)
		size_t streamCount = 0;							// ручьёв в растре (для лога)
		std::vector<std::string> warnings;				// правленные кривые не по рельефу, петли — в лог
		std::vector<DetailTile> detailTiles;			// детальная земля у русел (U-ложе): там, где русло режет рельеф

		// Сводка для лога
		size_t nodes = 0;
		size_t carvedCells = 0;
		size_t points = 0;
		float deepestLowering = 0.0f;
		float largestDischarge = 0.0f;
	};

	// Отпечаток того, по чему генерируются кривые русел: итоговая карта высот, приток, помощники и параметры генератора.
	// Не совпал с сохранённым (WaterSimulationSettings::streamsKey) — кривые генерируются заново
	static std::string generationKey( const HeightField& field, const WaterSimulationSettings& water );
	// Кривые русел по стоку (шаги 1–3 и цепочки: от истока или слива из озера до слияния, озера или края); расход в точках
	static bool generate( const HeightField& field, const WaterSimulationSettings& water, std::vector<StreamCurve>& curves );
	// Новые сгенерированные кривые вместо прежних несправленных: правленные и ручные остаются, сгенерированные в их полосе
	// обрезаются (остаток кончается на оси правленной)
	static std::vector<StreamCurve> merge( const std::vector<StreamCurve>& generated, const std::vector<StreamCurve>& existing,
										   const WaterChannelsSettings& channels );
	// Русла, ручьи и озёра по кривым. field — итоговая карта высот с ручными правками; water — строка WaterSimulation:
	// приток, помощники, параметры русел
	static bool build( const HeightField& field, const WaterSimulationSettings& water, const std::vector<StreamCurve>& curves,
					   Result& result );
	// Водосбор каждой клетки, м²: сток D8 по карте с заполненными низинами (тот же, что у build) — для карт эрозии
	static std::vector<float> catchment( const HeightField& field );
	// Растровые правки рельефа по опусканию: опускание и (вне детальных плиток) покраска дна галькой и очистка
	// растительности; в плитках покраска и очистка — в шейдерах по детальному врезу
	static std::vector<TerrainEdit> loweringEdits( const Result& result, const WaterChannelsSettings& channels );
};

}
