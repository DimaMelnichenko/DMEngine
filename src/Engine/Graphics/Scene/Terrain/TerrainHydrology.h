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
//    края; растр статичной воды (уровень ручья, течение, уровень озера) и водосбор вдоль русла (приток в режиме simulated);
// 6. камни русел — по отрезкам оси: дно (гуще на перекатах и на крутом), борта у уреза, валуны (Stone)
class TerrainHydrology
{
public:
	// Детальная земля у русел: плитка — detailTileCells клеток карты (лист квадродерева CDLOD), detailSamples точек на
	// клетку (0,25 м при клетке 1 м) и поле в тексель с каждой стороны: (detailTileCells · detailSamples + 2)² точек
	static constexpr uint32_t detailTileCells = 32;
	static constexpr uint32_t detailSamples = 4;
	// Мельче воды не видно — только мокрая земля, м: уровень поверхности детальной воды (water_detail.cs) и дно под водой
	// с мокрой землёй детального слоя весов (TerrainDetailSplat) — по одному правилу
	static constexpr float visibleWaterDepth = 0.01f;
	struct DetailTile
	{
		uint32_t x = 0;					// плитка по x и z мира (z — от ближнего края карты)
		uint32_t z = 0;
		std::vector<float> heights;		// высота, м; строки — по z мира вверх, тексели — по x
		std::vector<float> carve;		// врез русла (или чаши озера), м: покраска галькой и очистка растительности в шейдерах
		// Вода плитки — детальная вода у русел (WaterSimulation, docs/water.md): x — уровень воды, м (−1e9 — нет): ручья
		// ближайшего отрезка или озера, если оно не ниже; у ручья y, z — течение по Маннингу u₀ по X и Z мира, м/с, w —
		// гидравлический радиус русла R, м (течение на детальной сетке, water_flow.cs); у озера y, z, w — 0; тексели — как у
		// heights
		std::vector<DirectX::XMFLOAT4> water;
	};

	// Камень русла — часть кривой (docs/water.md, «Камни в русле»): стоит постоянно, одно и то же место при каждой загрузке.
	// Модель и поворот выбирает слой расстановки с placement stream_stones (Scatterer), течение тормозится о камень
	// (WaterSimulation, water_flow.cs)
	struct Stone
	{
		DirectX::XMFLOAT3 position;	// центр основания, м мира: y — самое низкое детальное дно под камнем
		float size;					// поперечник, м
		uint32_t seed;				// случайное число камня: модель, поворот
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
		std::vector<Stone> stones;						// камни русел

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
	// Водосбор каждой клетки, м²: обычный сток D8 по карте с заполненными низинами (у build и generate — без прямых по
	// сетке, flowDirections) — для карт эрозии
	static std::vector<float> catchment( const HeightField& field );
	// Растровые правки рельефа по опусканию: опускание, покраска дна галькой (splat-карта 1 м; вблизи у русел — детальный
	// слой весов по детальному врезу, TerrainDetailSplat) и (вне детальных плиток) очистка растительности — в плитках её
	// делают шейдеры по детальному врезу; у озёр при shaderLakeBeds (режим static) покраски и очистки нет — галька дна и
	// трава в воде по глубине в точке
	static std::vector<TerrainEdit> loweringEdits( const Result& result, bool shaderLakeBeds, int32_t bedLayer );
};

}
