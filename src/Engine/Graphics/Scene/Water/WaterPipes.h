#pragma once

#include <string>
#include "WaterField.h"
#include "SceneObject.h"
#include "DMComputeShader.h"

namespace GS
{

// Вода по рельефу в режиме simulated — модель виртуальных труб (как в From Dust) на сетке поля (WaterField): в ячейке
// высота воды и потоки в четыре соседа; приток — по водосбору вдоль русел конвейера (TerrainHydrology::channelFlow) и
// источникам-помощникам (строки WaterSources) и дождь, сток — края карты и испарение. При загрузке — озёра до уровня
// перелива и прогрев до установившегося течения, в игре — шаги по времени кадра. Шейдер — Shaders/water_simulation.cs.
// Приток считается и у статичной воды: его показывает «Show water» (SLOT_WATER_SOURCES)
class WaterPipes
{
public:
	bool initialize( WaterField& field, const WaterSimulationSettings& settings );
	// Водосбор вдоль русел конвейера — основа притока (при загрузке и перестройке русел)
	bool setChannelFlow( const TerrainHydrology::Result& hydrology );
	// Приток ячеек по водосбору — при загрузке, перестройке русел и смене настроек источников
	void buildSources( const WaterSimulationSettings& settings );
	// Озёра при загрузке: низины, куда впадает источник, — сразу до уровня перелива (иначе наполняются часами)
	bool fillLakes( const WaterSimulationSettings& settings );
	// Прогрев до установившегося течения при загрузке — порциями с ожиданием GPU; число шагов
	uint32_t warmUp( const WaterSimulationSettings& settings );
	// Шаги по времени кадра (с timestep — фиксированному), не больше maxStepsPerFrame; приток — заново, если сменились его
	// настройки. true — шаги были (поверхность пересчитать)
	bool advance( const FrameContext& frame, const WaterSimulationSettings& settings );
	// Расход ячеек, м³/с (сумма оттоков к соседям), — в DDS R32_FLOAT на сетке карты высот (строка 0 — дальний край по z);
	// ждёт GPU
	bool exportDischarge( const std::string& file, std::string& reason );
	const ShaderView& sourcesView() const { return m_sourcesView; }

private:
	// steps шагов в текущий командный список
	void step( uint32_t steps );

	WaterField* m_field = nullptr;
	WaterSimulationSettings m_sourcesBuilt;	// настройки, по которым посчитан приток
	float m_accumulated = 0.0f;				// время симуляции, ещё не отработанное шагами, с

	DMComputeShader m_sourcesShader;
	DMComputeShader m_fluxShader;
	DMComputeShader m_waterShader;
	DMComputeShader m_fillInitShader;
	DMComputeShader m_fillShader;
	DMComputeShader m_lakeInitShader;
	DMComputeShader m_lakeGrowShader;
	DMComputeShader m_lakeApplyShader;
	Texture m_flux;					// R32G32B32A32_FLOAT: потоки к соседям, м³/с
	StorageView m_fluxUAV;
	Texture m_sources;				// R32_FLOAT: приток, м/с
	StorageView m_sourcesUAV;
	ShaderView m_sourcesView;
	Texture m_memory;				// R32_FLOAT: глубина с памятью (mainWater читает прошлое значение)
	StorageView m_memoryUAV;
	Texture m_channelFlow;			// R32_FLOAT: водосбор вдоль русел конвейера (TerrainHydrology::channelFlow)
	ShaderView m_channelFlowView;
	Buffer m_helpers;				// float4 на источник-помощник: x, z, расход м³/с, σ м (WaterSources)
	ShaderView m_helpersView;
};

}
