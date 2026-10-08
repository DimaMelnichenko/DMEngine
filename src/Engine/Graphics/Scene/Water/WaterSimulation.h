#pragma once

#include "SceneObject.h"
#include "DMComputeShader.h"
#include "ShaderProgram.h"
#include "Terrain\GridMesh.h"
#include "Level\LevelSettings.h"
#include "Terrain\TerrainHeightSource.h"
#include "StreamRibbons.h"

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
// Режим static (WaterSimulationSettings::staticWater): вода не течёт — озёра наливаются при загрузке до перелива, ручьи —
// ленты по точкам из Tools/carve_channels.py (StreamRibbons), их вода для травы, мокрой земли и брызг — растр
// (mainStatic); как реки-сплайны и озёра в UE Water. Симуляция остаётся инструментом — режим simulated
class WaterSimulation : public SceneObject
{
public:
	using Settings = WaterSimulationSettings;

	WaterSimulation();
	// После террейна: сетка — его итоговая карта высот (с правками рельефа)
	bool initialize( const Settings& settings, const TerrainHeightSource& terrain );

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	void warmPipelines( const PassStates& states ) override;
	PropertyContainer* properties() override;
	bool savedWithLevel() const override { return true; }
	// Настройки с правками GUI — для сохранения уровня (строка WaterSimulation)
	Settings settings() const;
	// Расход ячеек, м³/с (сумма оттоков к соседям), — в DDS R32_FLOAT на сетке карты высот (строка 0 — дальний край
	// по z): по нему Tools/carve_channels.py режет русла. Ждёт GPU; false — воды нет или файл не записан (reason)
	bool exportDischarge( const std::string& file, std::string& reason );

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
	};

	// Раскладка — cbuffer WaterSurfaceBuffer (b2) в Shaders/water_surface.sh
	struct alignas( 16 ) SurfaceParameters
	{
		uint32_t size;
		float cellSize;
		float worldSize;
		uint32_t tilesPerSide;
		DirectX::XMFLOAT3 absorption;
		float flowPeriod;
		DirectX::XMFLOAT3 scatterColor;
		float rippleScale;
		float rippleStrength;
		float calmRipple;
		float refraction;
		float roughness;
		float foamSpeed;
		float foamShear;
		float padding[2];
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

	PropertyContainer m_properties;
	PropertyContainer m_surfaceProperties;	// подокно «Surface» — материал поверхности
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
	StreamRibbons m_streams;
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
};

}
