#pragma once

#include "SceneObject.h"
#include "Level\LevelSettings.h"
#include "Terrain\TerrainHeightSource.h"
#include "Terrain\TerrainHydrology.h"
#include "WaterField.h"
#include "WaterPipes.h"
#include "StaticWater.h"
#include "NearWaterFlow.h"
#include "WaterFoam.h"
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
//   вода 0,25 м; пена, которую несёт течение, — поле при загрузке (WaterFoam);
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

	// Детальная вода у русел в константах кадра (cb_waterDetail, water.sh): размер мира, м, и сторона плитки, м (0 — её нет)
	void frameConstants( const RenderView& view, FrameParameters& constants ) override;
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
	// Правили настройки пены (подокно «Surface») — поле пены заново (режим static); зовётся, когда редактор не тянет ползунок
	void applyFoamChanges();
	// Расход ячеек, м³/с (сумма оттоков к соседям), — в DDS R32_FLOAT на сетке карты высот (строка 0 — дальний край
	// по z) — для проверки. Ждёт GPU; false — воды нет или файл не записан (reason)
	bool exportDischarge( const std::string& file, std::string& reason );
	// Текстура воды для шейдеров (SLOT_WATER) — в DDS R32_FLOAT: <prefix>_depth.dds (глубина, м), <prefix>_vx.dds и
	// <prefix>_vz.dds (течение по X и Z мира, м/с); строка 0 — дальний край по z. Детальная вода у русел — так же, растром
	// 0,25 м на весь мир (−1e9 — плитки нет): <prefix>_detail_level.dds (уровень поверхности), _detail_depth, _detail_vx,
	// _detail_vz; окно подробного течения у камеры, если оно есть, — <prefix>_near_surface.dds, _near_vx, _near_vz,
	// _near_passage. Ждёт GPU
	bool exportState( const std::string& prefix, std::string& reason );
	// Камни русел расстановки с верхом по модели (Scatterer): препятствия окна подробного течения у камеры
	void setStoneTops( std::vector<StoneTop>&& tops ) { m_near.setStoneTops( std::move( tops ) ); }

private:
	// Настройки из окна для кадра (compute, отрисовка): заново — только когда окно правили (PropertyContainer::revision)
	const Settings& currentSettings() const;
	// Уровень поверхности и границы тайлов, затем детальная вода у русел
	void buildSurface();
	// Течение статичной воды (с поверхностью) и поле пены по нему
	void solveFlow( const Settings& settings );
	// Объём воды, наибольшая глубина и число мокрых ячеек — в лог (ждёт GPU)
	void logWaterSummary();

	PropertyContainer m_properties;
	PropertyContainer m_surfaceProperties;	// подокно «Surface» — материал поверхности
	PropertyContainer m_flowProperties;		// подокно «Flow» — течение статичной воды
	PropertyContainer m_nearProperties;		// подокно «Near flow» — подробное течение у камеры
	Settings m_initial;
	bool m_staticMode = false;				// режим static: шагов нет
	bool m_initialized = false;
	mutable Settings m_current;				// настройки кадра (currentSettings) и номер правки окна, по которому они прочитаны
	mutable uint64_t m_currentRevision = ~0ull;
	uint64_t m_flowRevision = ~0ull;		// номер правки «Flow», по которому сверено течение (applyFlowChanges)
	uint64_t m_foamRevision = ~0ull;		// номер правки «Surface», по которому сверено поле пены (applyFoamChanges)

	WaterField m_field;
	WaterPipes m_pipes;
	StaticWater m_staticWater;
	NearWaterFlow m_near;					// подробное течение у камеры (статичная вода)
	WaterFoam m_foam;
	WaterSurface m_surface;
	StreamCurves m_streams;
};

}
