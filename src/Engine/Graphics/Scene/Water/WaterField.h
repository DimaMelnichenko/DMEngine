#pragma once

#include "D3D\GpuResources.h"
#include "Level\LevelSettings.h"
#include "Terrain\TerrainHeightSource.h"
#include "Terrain\TerrainHydrology.h"
#include "Shaders\water_simulation_constants.h"

namespace GS
{

// Вода на сетке карты высот — общее у частей воды (WaterSimulation): сетка и рельеф, константы water_simulation.cs (b4) и
// поле, которое пишут трубы (WaterPipes) или статичная вода (StaticWater), а читают поверхность (WaterSurface) и все, кто
// стоит у воды: глубина и состояние для шейдеров — ресурс сцены SLOT_WATER (Shaders/water.sh)
struct WaterField
{
	static constexpr uint32_t groupSize = 8;		// numthreads в water_simulation.cs
	static constexpr float gravity = 9.81f;
	static constexpr float visibleDepth = TerrainHydrology::visibleWaterDepth;	// м: мельче воды не видно — только мокрая земля

	// Сетка — итоговая карта высот рельефа (с правками рельефа и руслами): глубина и состояние; false — карты нет
	bool initialize( const TerrainHeightSource& source );
	// Константы water_simulation.cs (b4) по настройкам: их читают и трубы, и статичная вода, и поверхность
	void setParameters( const WaterSimulationSettings& settings );

	const TerrainHeightSource* terrain = nullptr;
	uint32_t size = 0;
	float cellSize = 1.0f;
	uint32_t helperCount = 0;	// источников-помощников в буфере притока (WaterPipes)
	Buffer constants;			// cbuffer WaterSimulationBuffer (b4)
	Texture depth;				// R32_FLOAT: глубина, м
	StorageView depthUAV;
	ShaderView depthView;
	Texture state;				// R16G16B16A16_FLOAT: глубина, скорость X и Z, глубина с памятью — для шейдеров (SLOT_WATER)
	StorageView stateUAV;
	ShaderView stateView;

private:
};

}
