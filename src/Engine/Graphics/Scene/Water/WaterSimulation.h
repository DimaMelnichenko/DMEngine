#pragma once

#include <memory>
#include <vector>
#include "SceneObject.h"
#include "DMComputeShader.h"
#include "ShaderProgram.h"
#include "Terrain\GridMesh.h"
#include "Level\LevelSettings.h"
#include "Terrain\TerrainHeightSource.h"
#include "Terrain\TerrainHydrology.h"

namespace GS
{

// Вода по рельефу — поле высот на сетке карты высот террейна (модель виртуальных труб, как в From Dust): в ячейке
// высота воды и потоки в четыре соседа, вода течёт по рельефу сама — ручьи, заводи и озёра в низинах получаются из
// рельефа и притока. Приток — по карте водосбора (ручей рождается там, где водосбор большой) и дождь, сток — края
// карты и испарение. При загрузке симуляция просчитывается до установившегося течения, в игре идёт шагами по времени
// кадра. Результат — текстура для шейдеров в слоте сцены SLOT_WATER (Shaders/water.sh): глубина и скорость. Шейдер —
// Shaders/water_simulation.cs, настройки — строка WaterSimulation уровня, подробно — docs/water.md. Поверхность воды
// объект рисует сам в проходе transparent (читает цвет и глубину сцены): тайлы сетки с водой, отобранные на GPU
// (Shaders/water_surface.cs), — один косвенный вызов (water.vs, water.ps).
// Режим static (WaterSimulationSettings::staticWater): вода не течёт — озёра и ручьи из конвейера рельефа и воды
// (TerrainHydrology) одним полем уровня — растр (mainStatic): по нему та же поверхность тайлов, что у симуляции, трава,
// мокрая земля и брызги; течение озёр — установившийся расчёт при загрузке (solveFlow, Shaders/water_flow.cs).
// Симуляция остаётся инструментом — режим simulated
class WaterSimulation : public SceneObject
{
public:
	using Settings = WaterSimulationSettings;

	WaterSimulation();
	// После террейна: сетка — его итоговая карта высот (с правками рельефа)
	// hydrology — конвейер рельефа и воды (CDLODTerrain::hydrology): приток вдоль русел, ленты ручьёв и растр статичной воды
	bool initialize( const Settings& settings, const TerrainHeightSource& terrain, const TerrainHydrology::Result& hydrology );

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	void warmPipelines( const PassStates& states ) override;
	PropertyContainer* properties() override;
	bool savedWithLevel() const override { return true; }
	// Настройки с правками GUI — для сохранения уровня (строка WaterSimulation); кривые русел — streamCurves()
	Settings settings() const;

	// Кривые русел (редактор: подокно «Streams» и точки во вьюпорте). streamCurves — с множителями и включением из окна и
	// флагом правки; curves — как есть (точки для вьюпорта)
	std::vector<StreamCurve> streamCurves() const;
	const std::vector<StreamCurve>& curves() const { return m_curves; }
	bool initialized() const { return m_initialized; }
	// Точка кривой — x, z мира (перетаскивание: русла не перестраиваются до finishCurveEdit)
	void setCurvePoint( size_t curve, size_t point, DirectX::XMFLOAT2 position );
	// Новая точка после after (−1 — в начало: перед первой); удаление — если останется не меньше двух
	bool insertCurvePoint( size_t curve, int after, DirectX::XMFLOAT2 position );
	bool removeCurvePoint( size_t curve, size_t point );
	// Правка кривой закончена (отпустили гизмо, вставили или удалили точку): кривая правленная, русла — перестроить
	void finishCurveEdit( size_t curve );
	// Подокно кривой в «Streams» — для выбора в Details
	PropertyContainer* curveProperties( size_t curve );
	// Русла надо перестроить: правили кривые, множители ручьёв или форму русел в «Channels» (параметры генератора —
	// при следующей загрузке); сбрасывается rebuild
	bool rebuildRequested() const;
	// Вода по перестроенным руслам (CDLODTerrain::rebuildChannels): растр, ленты, приток, поверхность
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
	// Раскладка — cbuffer WaterSimulationBuffer (b4) в water_simulation.cs
	struct alignas( 16 ) Parameters
	{
		uint32_t size;
		float cellSize;
		float timeStep;
		float gravity;
		float rain;
		float evaporation;
		float sourceRate;
		float heightMultiplier;
		float heightOffset;
		float logFlowStart;
		float logFlowFull;
		int32_t sourceRadius;
		float manning;
		uint32_t helperCount;		// источников-помощников в буфере m_helpers
		float padding[2];
	};

	// Раскладка — cbuffer WaterTilesBuffer (b5) в water_surface.cs
	struct alignas( 16 ) TilesParameters
	{
		DirectX::XMFLOAT4 planes[6];
		uint32_t indexCount;
		uint32_t tilesPerSide;
		float visibleDepth;
		float worldSize;
		DirectX::XMFLOAT3 cameraPosition;	// главного вида: ближе detailDistance — детальная поверхность
		float detailDistance;				// м; 0 — детальной воды нет
		uint32_t detailIndexCount;			// индексов сетки детальной плитки
		uint32_t detailPadding[3];
	};

	// Раскладка — cbuffer WaterDetailBuffer (b5) в Shaders/water_detail.cs
	struct alignas( 16 ) DetailParameters
	{
		float heightMultiplier;
		float heightOffset;
		float worldSize;
		float tileSize;
		float visibleDepth;
		uint32_t side;		// текселей плитки по стороне (с полем)
		float padding[2];
	};

	// Раскладка — cbuffer WaterSurfaceBuffer (b2) в Shaders/water_surface.sh
	struct alignas( 16 ) SurfaceParameters
	{
		uint32_t size;
		float cellSize;
		float worldSize;
		uint32_t tilesPerSide;
		DirectX::XMFLOAT3 absorption;
		float rippleStretch;
		DirectX::XMFLOAT3 scatterColor;
		float rippleScale;
		float rippleStrength;
		float calmRipple;
		float refraction;
		float roughness;
		float foamSpeed;
		float foamShear;
		float detailDistance;	// м: ближе — детальная поверхность у русел (0 — её нет)
		float foamSlope;		// пена слива — где поверхность детальной воды круче, м на м (WaterChannelsSettings::foamSlope)
		// Точная земля — урез по ней (water.ps): карта высот (слот 3) в метры и детальная плитка (terrain_detail.sh)
		float terrainHeightMultiplier;
		float terrainHeightOffset;
		float terrainDetailTile;	// сторона детальной плитки, м; 0 — её нет
		float surfacePadding;
	};

	// Раскладка — cbuffer WaterFlowBuffer (b5) в water_flow.cs
	struct alignas( 16 ) FlowParameters
	{
		uint32_t tileCount;
		uint32_t tilesPadding[3];
		float timeStep;
		float viscosity;
		float friction;
		float minDepth;
		float screen;
		float relaxation;
		float layer;
		float stoneDrag;	// dt · Cd / 2: сопротивление камней русла
	};

	// Раскладка — cbuffer WaterDetailFlowBuffer (b6) в water_flow.cs (FLOW_DETAIL)
	struct alignas( 16 ) DetailFlowParameters
	{
		uint32_t cells;			// ячеек плитки по стороне (без поля)
		uint32_t tilesPerSide;	// плиток по стороне мира
		float step;				// сторона ячейки, м
		float wetDepth;			// ячейка вне плиток мокрая, если по сетке 1 м глубже, м
	};

	void setParameters( const Settings& settings );
	// Приток ячеек по карте водосбора — при загрузке и при смене настроек источников
	void buildSources( const Settings& settings );
	// Озёра при загрузке: низины, куда впадает источник, — сразу до уровня перелива (иначе наполняются часами)
	bool fillLakes();
	// steps шагов симуляции в текущий командный список
	void step( uint32_t steps );
	// Объём воды, наибольшая глубина и число мокрых ячеек — в лог (ждёт GPU)
	void logWaterSummary();
	bool createSurface();
	// Уровень поверхности и границы тайлов — после шагов симуляции
	void buildSurface();
	// Видимые тайлы главного вида — в список экземпляров косвенного вызова
	void cullTiles( const RenderView& view );
	// Режим static: озёра и ручьи в текстуру для шейдеров (mainStatic)
	bool buildStaticWater( const Settings& settings );
	// Текстуры из данных конвейера: водосбор вдоль русел и растр статичной воды; тайлы озёр — область счёта течения
	bool createHydrologyTextures( const TerrainHydrology::Result& hydrology );
	// Режим static: установившееся течение озёр (water_flow.cs) поверх mainStatic — порциями с ожиданием GPU
	bool createFlowResources();
	// Сводка расчёта (она же — в лог); пусто — расчёта не было
	std::string solveFlow( const Settings::Flow& flow );
	// Течение на детальной сетке у русел (water_flow.cs с FLOW_DETAIL) — после solveFlow и buildSurface: решение 1 м —
	// начало и граница, итог — течение мокрых ячеек детальной воды (m_detailWater). Массивы счёта — только на время расчёта
	std::string solveDetailFlow( const Settings::Flow& flow );
	// Течение статичной воды целиком — solveFlow, поверхность, solveDetailFlow — или из кэша на диске (после
	// buildStaticWater): файл cache\water\flow_<ключ>.bin, ключ — отпечаток входа (m_flowInputKey), настройки «Flow»,
	// высота рельефа, шейдер течения и версия расчёта. При загрузке, перестройке русел и правке «Flow»
	void solveStaticFlow( const Settings::Flow& flow );
	std::string flowCachePath( const Settings::Flow& flow ) const;
	bool loadFlowCache( const std::string& path );
	void saveFlowCache( const std::string& path, const std::string& coarseSummary, const std::string& detailSummary );

	PropertyContainer m_properties;
	PropertyContainer m_surfaceProperties;	// подокно «Surface» — материал поверхности
	PropertyContainer m_flowProperties;		// подокно «Flow» — течение статичной воды
	Settings m_initial;
	Settings m_sourcesBuilt;		// настройки, по которым посчитаны источники
	const TerrainHeightSource* m_terrain = nullptr;
	uint32_t m_size = 0;
	float m_cellSize = 1.0f;
	float m_accumulated = 0.0f;		// время симуляции, ещё не отработанное шагами, с
	bool m_initialized = false;

	DMComputeShader m_sourcesShader;
	DMComputeShader m_fluxShader;
	DMComputeShader m_waterShader;
	DMComputeShader m_fillInitShader;
	DMComputeShader m_fillShader;
	DMComputeShader m_lakeInitShader;
	DMComputeShader m_lakeGrowShader;
	DMComputeShader m_lakeApplyShader;
	DMComputeShader m_staticShader;
	bool m_static = false;			// режим static: шагов нет
	// Из конвейера (TerrainHydrology): водосбор вдоль русел — приток (mainSources); растр статичной воды — ручьи и маска
	// озёр (режим static)
	Texture m_flowTexture;
	ShaderView m_flowView;
	Texture m_staticWaterTexture;
	ShaderView m_staticWaterView;
	Texture m_stoneDrag;			// R32G32_FLOAT: площадь камней поперёк потока на м² дна — своя и средняя вокруг (water_flow.cs)
	ShaderView m_stoneDragView;
	const ShaderView* m_staticWaterMap = nullptr;
	PropertyContainer m_channelsProperties;	// «Channels»: WaterChannelsSettings — при следующей загрузке
	// «Streams»: кривые русел — включение и множители, подокно на кривую (порядок — как m_initial.streams)
	PropertyContainer m_streamsProperties;
	std::vector<std::unique_ptr<PropertyContainer>> m_streamProperties;
	// Кривые русел: нынешние (точки правит вьюпорт) и как построены — по ним видно, что множители, включение и форма
	// русел изменились (rebuildRequested)
	std::vector<StreamCurve> m_curves;
	std::vector<StreamCurve> m_builtCurves;
	WaterChannelsSettings m_builtChannels;
	bool m_curvesDirty = false;
	Buffer m_constantBuffer;

	Texture m_water;				// R32_FLOAT: глубина, м
	Texture m_flux;					// R32G32B32A32_FLOAT: потоки к соседям, м³/с
	Texture m_sources;				// R32_FLOAT: приток, м/с
	Texture m_output;				// R16G16B16A16_FLOAT: глубина, скорость X и Z, глубина с памятью — для шейдеров
	Texture m_memory;				// R32_FLOAT: глубина с памятью (mainWater читает прошлое значение)
	StorageView m_memoryUAV;
	StorageView m_waterUAV;
	StorageView m_fluxUAV;
	StorageView m_sourcesUAV;
	StorageView m_outputUAV;
	ShaderView m_sourcesView;
	ShaderView m_outputView;
	ShaderView m_waterView;
	Buffer m_helpers;				// float4 на источник-помощник: x, z, расход м³/с, σ м (WaterSources)
	ShaderView m_helpersView;
	uint32_t m_helperCount = 0;

	// Течение статичной воды (water_flow.cs): только в рамке озёр с полем
	DMComputeShader m_flowInitShader;
	DMComputeShader m_flowFacesShader;
	DMComputeShader m_flowAdvectShader;
	DMComputeShader m_flowViscosityShader;
	DMComputeShader m_flowDivergenceShader;
	DMComputeShader m_flowRedShader;
	DMComputeShader m_flowBlackShader;
	DMComputeShader m_flowProjectShader;
	DMComputeShader m_flowOutputShader;
	Buffer m_flowBuffer;			// FlowParameters
	Texture m_flowInfo;				// R32G32B32A32_FLOAT: заданная скорость, вид ячейки, глубина
	Texture m_flowVelocity;			// R32G32_FLOAT: скорость на восточной и южной грани
	Texture m_flowAdvected;			// R32G32_FLOAT: после переноса — до вязкости и трения
	Texture m_flowPressure;			// R32_FLOAT
	Texture m_flowDivergence;		// R32_FLOAT
	StorageView m_flowInfoUAV;
	StorageView m_flowVelocityUAV;
	StorageView m_flowAdvectedUAV;
	StorageView m_flowPressureUAV;
	StorageView m_flowDivergenceUAV;
	// Тайлы счёта 8 × 8 (x, y тайла): с ячейками озёр и соседние — ручьи у берега и сухой берег вокруг; пусто — озёр нет
	std::vector<DirectX::XMUINT2> m_flowTileList;
	Buffer m_flowTiles;
	ShaderView m_flowTilesView;
	Settings::Flow m_flowBuilt;		// по каким настройкам посчитано течение
	// Отпечаток входа течения (createHydrologyTextures): итоговый рельеф, растр статичной воды, камни, плитки детальной
	// земли и воды; 0 — кэша нет (нет карты высот на CPU)
	uint64_t m_flowInputKey = 0;
	DMComputeShader m_flowRestoreShader;

	// Поверхность
	DMComputeShader m_surfaceShader;
	DMComputeShader m_tilesResetShader;
	DMComputeShader m_tilesShader;
	ShaderProgram m_surfaceProgram;
	int m_surfacePhase = -1;
	GridMesh m_tileMesh;
	uint32_t m_tilesPerSide = 0;
	bool m_surfaceDirty = true;		// шаги симуляции были — уровень поверхности пересчитать
	Texture m_level;				// R32_FLOAT: уровень поверхности, м; −1e30 — сухо
	StorageView m_levelUAV;
	ShaderView m_levelView;
	Buffer m_tileBounds;			// float2 на тайл: наименьший и наибольший уровень
	StorageView m_tileBoundsUAV;
	ShaderView m_tileBoundsView;
	Buffer m_tileList;				// uint на видимый тайл — экземпляры вызова
	StorageView m_tileListUAV;
	ShaderView m_tileListView;
	Buffer m_drawArgs;				// команда ExecuteIndirect (24 байта) и число команд
	StorageView m_drawArgsUAV;
	Buffer m_tilesBuffer;			// TilesParameters
	Buffer m_surfaceBuffer;			// SurfaceParameters
	Texture m_ripple;				// карта ряби (WaterRipples)
	ShaderView m_rippleView;

	// Детальная вода у русел (режим static, Shaders/water_detail.cs): плитки — те же, что у детальной земли
	// (TerrainHydrology::DetailTile, тот же индекс SLOT_TERRAIN_DETAIL_INDEX), 0,25 м с полем в тексель
	void buildDetailSurface();
	// Массивы детальной воды по плиткам конвейера (createHydrologyTextures): статичная вода плиток и цель детальной поверхности
	bool createDetailTextures( const TerrainHydrology::Result& hydrology );
	uint32_t m_detailTileCount = 0;
	uint32_t m_detailSide = 0;
	float m_detailTileSize = 0.0f;
	Texture m_detailStatic;			// RGBA32F массив: уровень воды, u₀ по X и Z мира, R русла (DetailTile::water)
	ShaderView m_detailStaticView;
	Texture m_detailWater;			// RGBA32F массив: уровень поверхности, течение X, Z, глубина — SLOT_WATER_DETAIL
	StorageView m_detailWaterUAV;
	ShaderView m_detailWaterView;
	std::vector<DirectX::XMUINT2> m_detailCoords;	// плитка среза по сетке мира (x, z) — выгрузка
	Buffer m_detailTileCoords;		// uint2 на срез: плитка по сетке мира (x, z)
	ShaderView m_detailTileCoordsView;
	Buffer m_detailBounds;			// на плитку: ~наименьший и наибольший уровень (orderedBits) — отбор mainTiles
	StorageView m_detailBoundsUAV;
	ShaderView m_detailBoundsView;
	Buffer m_detailTileList;		// uint на видимую детальную плитку — экземпляры её вызова
	StorageView m_detailTileListUAV;
	ShaderView m_detailTileListView;
	Buffer m_detailDrawArgs;		// команда ExecuteIndirect и число команд
	StorageView m_detailDrawArgsUAV;
	Buffer m_detailBuffer;			// DetailParameters
	DMComputeShader m_detailSurfaceShader;
	GridMesh m_detailMesh;			// сетка детальной плитки: вершина на тексель (с полем)
	int m_detailPhase = -1;
	// Течение детальной сетки (solveDetailFlow): блоки 8 × 8 плиток с водой и сухим текселем вокруг — срез и номер блока
	// в плитке (строка блоков — по Z мира); плитка мира → срез (−1 — нет); камни русла — препятствия
	std::vector<DirectX::XMUINT2> m_detailBlockList;
	Buffer m_detailBlocks;
	ShaderView m_detailBlocksView;
	std::vector<int32_t> m_detailSliceOf;
	uint32_t m_detailTilesPerSide = 0;
	std::vector<TerrainHydrology::Stone> m_detailStones;
	DMComputeShader m_detailFlowInitShader;
	DMComputeShader m_detailFlowFacesShader;
	DMComputeShader m_detailFlowAdvectShader;
	DMComputeShader m_detailFlowViscosityShader;
	DMComputeShader m_detailFlowNudgeShader;
	DMComputeShader m_detailFlowDivergenceShader;
	DMComputeShader m_detailFlowRedShader;
	DMComputeShader m_detailFlowBlackShader;
	DMComputeShader m_detailFlowProjectShader;
	DMComputeShader m_detailFlowOutputShader;
	Buffer m_detailFlowBuffer;		// DetailFlowParameters
	DMComputeShader m_detailFlowRestoreShader;
};

}
