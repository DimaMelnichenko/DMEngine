#pragma once

#include "SceneObject.h"
#include "Level\LevelSettings.h"
#include "Terrain\TerrainHeightSource.h"
#include "Terrain\TerrainHydrology.h"
#include "WaterField.h"
#include "WaterPipes.h"
#include "StaticWater.h"
#include "WaterSurface.h"
#include "StreamCurves.h"

namespace GS
{

// Вода уровня (строка WaterSimulation, docs/water.md) — объект сцены из частей на общем поле (WaterField: сетка карты высот
// террейна, глубина и состояние — текстура для шейдеров в слоте сцены SLOT_WATER, Shaders/water.sh):
// - режим simulated — трубы (WaterPipes, как в From Dust): вода течёт по рельефу сама, приток — по водосбору вдоль русел
//   конвейера и источникам-помощникам, при загрузке — налив озёр и прогрев, в игре — шаги по времени кадра;
// - режим static (WaterSimulationSettings::staticWater) — статичная вода (StaticWater): озёра и ручьи конвейера рельефа и
//   воды (TerrainHydrology) одним полем уровня, течение — установившийся расчёт при загрузке или из кэша, у русел — детальная
//   вода 0,25 м;
// - поверхность (WaterSurface) — свой вызов в проходе transparent: тайлы с водой, отобранные на GPU;
// - кривые русел в редакторе (StreamCurves): подокна «Channels» и «Streams», точки во вьюпорте.
// Окно «Water simulation» — приток и испарение, подокна «Surface», «Flow», «Channels», «Streams»
class WaterSimulation : public SceneObject
{
public:
	using Settings = WaterSimulationSettings;

	WaterSimulation();
	// После террейна: сетка — его итоговая карта высот (с правками рельефа)
	// hydrology — конвейер рельефа и воды (TerrainPipeline::hydrology): приток вдоль русел, ленты ручьёв и растр статичной воды
	bool initialize( const Settings& settings, const TerrainHeightSource& terrain, const TerrainHydrology::Result& hydrology );

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	void warmPipelines( const PassStates& states ) override;
	PropertyContainer* properties() override;
	bool savedWithLevel() const override { return true; }
	// Настройки с правками GUI — для сохранения уровня (строка WaterSimulation); кривые русел — streamCurves()
	Settings settings() const;
	bool initialized() const { return m_initialized; }

	// Кривые русел в редакторе: окно, точки, что правили (вьюпорт, команда stream)
	StreamCurves& streams() { return m_streams; }
	std::vector<StreamCurve> streamCurves() const { return m_streams.streamCurves(); }
	// Русла надо перестроить: правили кривые, множители ручьёв или форму русел в «Channels» (параметры генератора —
	// при следующей загрузке); сбрасывается rebuild
	bool rebuildRequested() const { return m_initialized && m_streams.rebuildRequested(); }
	// Вода по перестроенным руслам (TerrainPipeline::rebuildChannels): растр, ленты, приток, поверхность
	bool rebuild( const TerrainHydrology::Result& hydrology );
	// Правили подокно «Flow» — течение озёр заново (режим static); зовётся, когда редактор не тянет ползунок
	void applyFlowChanges();
	// Расход ячеек, м³/с (сумма оттоков к соседям), — в DDS R32_FLOAT на сетке карты высот (строка 0 — дальний край
	// по z) — для проверки. Ждёт GPU; false — воды нет или файл не записан (reason)
	bool exportDischarge( const std::string& file, std::string& reason );
	// Текстура воды для шейдеров (SLOT_WATER) — в DDS R32_FLOAT: <prefix>_depth.dds (глубина, м), <prefix>_vx.dds и
	// <prefix>_vz.dds (течение по X и Z мира, м/с); строка 0 — дальний край по z. Детальная вода у русел — так же, растром
	// 0,25 м на весь мир (−1e9 — плитки нет): <prefix>_detail_level.dds (уровень поверхности), _detail_depth, _detail_vx,
	// _detail_vz. Ждёт GPU
	bool exportState( const std::string& prefix, std::string& reason );
	// Детальная вода у русел — константы кадра (cb_waterDetail, water.sh): размер мира, м, и сторона плитки, м (0 — её нет)
	DirectX::XMFLOAT4 detailParameters() const;

private:
	// Уровень поверхности и границы тайлов, затем детальная вода у русел
	void buildSurface();
	// Объём воды, наибольшая глубина и число мокрых ячеек — в лог (ждёт GPU)
	void logWaterSummary();

	PropertyContainer m_properties;
	PropertyContainer m_surfaceProperties;	// подокно «Surface» — материал поверхности
	PropertyContainer m_flowProperties;		// подокно «Flow» — течение статичной воды
	Settings m_initial;
	bool m_staticMode = false;				// режим static: шагов нет
	bool m_initialized = false;

	WaterField m_field;
	WaterPipes m_pipes;
	StaticWater m_staticWater;
	WaterSurface m_surface;
	StreamCurves m_streams;
};

}
