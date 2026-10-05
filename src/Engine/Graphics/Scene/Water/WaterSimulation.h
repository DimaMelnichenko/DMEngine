#pragma once

#include "SceneObject.h"
#include "DMComputeShader.h"
#include "Level\LevelSettings.h"
#include "Terrain\TerrainHeightSource.h"

namespace GS
{

// Вода по рельефу — поле высот на сетке карты высот террейна (модель виртуальных труб, как в From Dust): в ячейке
// высота воды и потоки в четыре соседа, вода течёт по рельефу сама — ручьи, заводи и озёра в низинах получаются из
// рельефа и притока. Приток — по карте водосбора (ручей рождается там, где водосбор большой) и дождь, сток — края
// карты и испарение. При загрузке симуляция просчитывается до установившегося течения, в игре идёт шагами по времени
// кадра. Результат — текстура для шейдеров в слоте сцены SLOT_WATER (Shaders/water.sh): глубина и скорость. Шейдер —
// Shaders/water_simulation.cs, настройки — строка WaterSimulation уровня, подробно — docs/water.md
class WaterSimulation : public SceneObject
{
public:
	using Settings = WaterSimulationSettings;

	WaterSimulation();
	// После террейна: сетка — его итоговая карта высот (с правками рельефа)
	bool initialize( const Settings& settings, const TerrainHeightSource& terrain );

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}
	PropertyContainer* properties() override;

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
		float padding[3];
	};

	// Текущие значения GUI
	Settings settings() const;
	void setParameters( const Settings& settings );
	// Приток ячеек по карте водосбора — при загрузке и при смене настроек источников
	void buildSources( const Settings& settings );
	// Озёра при загрузке: низины, куда впадает источник, — сразу до уровня перелива (иначе наполняются часами)
	bool fillLakes();
	// steps шагов симуляции в текущий командный список
	void step( uint32_t steps );
	// Объём воды, наибольшая глубина и число мокрых ячеек — в лог (ждёт GPU)
	void logWaterSummary();

	PropertyContainer m_properties;
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
	Buffer m_constantBuffer;

	Texture m_water;				// R32_FLOAT: глубина, м
	Texture m_flux;					// R32G32B32A32_FLOAT: потоки к соседям, м³/с
	Texture m_sources;				// R32_FLOAT: приток, м/с
	Texture m_output;				// R16G16B16A16_FLOAT: глубина, скорость X и Z — для шейдеров
	StorageView m_waterUAV;
	StorageView m_fluxUAV;
	StorageView m_sourcesUAV;
	StorageView m_outputUAV;
	ShaderView m_sourcesView;
	ShaderView m_outputView;
};

}
