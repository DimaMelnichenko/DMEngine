#pragma once

#include <functional>
#include <string>
#include <vector>
#include "WaterField.h"
#include "ComputeShader.h"
#include "Shaders\water_flow_constants.h"
#include "Shaders\water_detail_flow_constants.h"
#include "Shaders\water_detail_constants.h"

namespace GS
{

// Статичная вода (режим static, WaterSimulationSettings::staticWater): озёра и ручьи конвейера рельефа и воды
// (TerrainHydrology) одним полем уровня — растр в поле (mainStatic, water_simulation.cs), без шагов. Течение ручьёв и озёр —
// установившийся расчёт (Shaders/water_flow.cs: мелкая вода под «жёсткой крышкой» на сетке MAC, ручей держит сила тяжести
// вдоль уклона) или копия из кэша cache\water\flow_<ключ>.bin; у русел — детальная вода на плитках детальной земли (0,25 м,
// SLOT_WATER_DETAIL) с течением на своей сетке, вложенной в сетку 1 м. Камни русел тормозят течение; их сопротивление
// берёт и поверхность. Текстуры из конвейера (растр, сопротивление камней) — и в режиме simulated
class StaticWater
{
public:
	using Flow = WaterSimulationSettings::Flow;

	// initial — настройки при загрузке: константы поля (b4) в расчёте течения
	bool initialize( WaterField& field, const WaterSimulationSettings& initial );
	// Данные конвейера — при загрузке и перестройке русел: растр статичной воды, сопротивление камней, тайлы счёта течения, у
	// статичной (enabled) — детальная вода у русел; отпечаток входа течения — ключ кэша
	bool createTextures( const TerrainHydrology::Result& hydrology, bool enabled );
	// Озёра и ручьи — в поле (mainStatic): глубина и состояние; у озёр течения нет — его даёт solve
	bool build( const WaterSimulationSettings& settings );
	// Шейдеры и текстуры расчёта течения — один раз при загрузке
	bool createFlowResources();
	// Течение целиком — сетка 1 м, поверхность (buildSurface: детальная вода берёт течение сетки 1 м), детальная сетка — или из
	// кэша на диске: ключ — отпечаток входа, настройки «Flow», высота рельефа, шейдер течения и версия расчёта. При загрузке,
	// перестройке русел и правке «Flow»
	void solve( const Flow& flow, const std::function<void()>& buildSurface );
	// По каким настройкам посчитано течение
	const Flow& flowBuilt() const { return m_flowBuilt; }
	// Детальная вода у русел: уровень и глубина плиток (mainDetailSurface, water_detail.cs) — после поверхности сетки 1 м
	void buildDetailSurface();
	uint32_t detailTileCount() const { return m_detailTileCount; }
	float detailTileSize() const { return m_detailTileSize; }
	const ShaderView& detailWaterView() const { return m_detailWaterView; }
	const ShaderView& detailBoundsView() const { return m_detailBoundsView; }
	// Сопротивление камней русла: площадь поперёк потока на м² дна — своя и средняя вокруг (R32G32_FLOAT)
	const ShaderView& stoneDragView() const { return m_stoneDragView; }
	// Детальная вода — растром 0,25 м на весь мир (−1e9 — плитки нет): <prefix>_detail_level.dds (уровень поверхности),
	// _detail_depth, _detail_vx, _detail_vz; строка 0 — дальний край по z. Ждёт GPU
	bool exportDetail( const std::string& prefix, std::string& reason );

private:



	// Массивы детальной воды по плиткам конвейера: статичная вода плиток и детальная вода; блоки счёта течения
	bool createDetailTextures( const TerrainHydrology::Result& hydrology );
	// Течение сетки 1 м (water_flow.cs) поверх mainStatic — порциями с ожиданием GPU; сводка (она же — в лог), пусто — расчёта не было
	std::string solveFlow( const Flow& flow );
	// Течение на детальной сетке у русел (water_flow.cs с FLOW_DETAIL) — после solveFlow и поверхности: решение 1 м — начало
	// и граница, итог — течение мокрых ячеек детальной воды. Массивы счёта — только на время расчёта
	std::string solveDetailFlow( const Flow& flow );
	std::string flowCachePath( const Flow& flow ) const;
	bool loadFlowCache( const std::string& path, const std::function<void()>& buildSurface );
	void saveFlowCache( const std::string& path, const std::string& coarseSummary, const std::string& detailSummary );

	WaterField* m_field = nullptr;
	WaterSimulationSettings m_initial;
	bool m_enabled = false;			// режим static
	ComputeShader m_staticShader;
	// Из конвейера (TerrainHydrology): растр статичной воды — ручьи и маска озёр
	Texture m_staticWaterTexture;
	ShaderView m_staticWaterView;
	Texture m_stoneDrag;			// R32G32_FLOAT: площадь камней поперёк потока на м² дна — своя и средняя вокруг (water_flow.cs)
	ShaderView m_stoneDragView;

	// Течение сетки 1 м: только в рамке озёр и ручьёв с полем
	ComputeShader m_flowInitShader;
	ComputeShader m_flowFacesShader;
	ComputeShader m_flowAdvectShader;
	ComputeShader m_flowViscosityShader;
	ComputeShader m_flowDivergenceShader;
	ComputeShader m_flowRedShader;
	ComputeShader m_flowBlackShader;
	ComputeShader m_flowProjectShader;
	ComputeShader m_flowOutputShader;
	ComputeShader m_flowRestoreShader;
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
	// Тайлы счёта 8 × 8 (x, y тайла): с ячейками озёр и ручьёв и соседние — сухой берег вокруг; пусто — воды нет
	std::vector<DirectX::XMUINT2> m_flowTileList;
	Buffer m_flowTiles;
	ShaderView m_flowTilesView;
	Flow m_flowBuilt;				// по каким настройкам посчитано течение
	// Отпечаток входа течения (createTextures): итоговый рельеф, растр статичной воды, камни, плитки детальной земли и воды;
	// 0 — кэша нет (нет карты высот на CPU)
	uint64_t m_flowInputKey = 0;

	// Детальная вода у русел (Shaders/water_detail.cs): плитки — те же, что у детальной земли (TerrainHydrology::DetailTile,
	// тот же индекс SLOT_TERRAIN_DETAIL_INDEX), 0,25 м с полем в тексель
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
	Buffer m_detailBounds;			// на плитку: ~наименьший и наибольший уровень (orderedBits) — отбор плиток поверхности
	StorageView m_detailBoundsUAV;
	ShaderView m_detailBoundsView;
	Buffer m_detailBuffer;			// DetailParameters
	ComputeShader m_detailSurfaceShader;
	// Течение детальной сетки (solveDetailFlow): блоки 8 × 8 плиток с водой и сухим текселем вокруг — срез и номер блока
	// в плитке (строка блоков — по Z мира); плитка мира → срез (−1 — нет); камни русла — препятствия
	std::vector<DirectX::XMUINT2> m_detailBlockList;
	Buffer m_detailBlocks;
	ShaderView m_detailBlocksView;
	std::vector<int32_t> m_detailSliceOf;
	uint32_t m_detailTilesPerSide = 0;
	std::vector<TerrainHydrology::Stone> m_detailStones;
	ComputeShader m_detailFlowInitShader;
	ComputeShader m_detailFlowFacesShader;
	ComputeShader m_detailFlowAdvectShader;
	ComputeShader m_detailFlowViscosityShader;
	ComputeShader m_detailFlowNudgeShader;
	ComputeShader m_detailFlowDivergenceShader;
	ComputeShader m_detailFlowRedShader;
	ComputeShader m_detailFlowBlackShader;
	ComputeShader m_detailFlowProjectShader;
	ComputeShader m_detailFlowOutputShader;
	ComputeShader m_detailFlowRestoreShader;
	Buffer m_detailFlowBuffer;		// DetailFlowParameters
};

}
