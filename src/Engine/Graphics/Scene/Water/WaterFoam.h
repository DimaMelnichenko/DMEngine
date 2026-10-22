#pragma once

#include <string>
#include "WaterField.h"
#include "StaticWater.h"
#include "ComputeShader.h"
#include "WaterGrid.h"
#include "Shaders\water_foam_constants.h"

namespace GS
{

// Поле пены статичной воды (Shaders/water_foam.cs, docs/water.md, «Пена»): пена, которую несёт течение. Источник — быстрая
// вода, сдвиг скорости (норма тензора скоростей деформации) и падение со ступени (нижняя часть и котёл; верх — чистый,
// foam_fall_*); пена у источника растёт
// к нему, переносится течением, тает за время жизни (foam_lifetime на быстрой воде, foam_still_lifetime — в стоячей) и
// густеет там, где течение сходится (ручей входит в плёс).
// Течение установившееся, поэтому поле считается при загрузке после течения и при правке настроек пены, а поверхность
// (water.ps) берёт его одной выборкой. Две сетки: детальная у русел (плитки детальной воды, 0,25 м) и сетка 1 м — в клетках
// у русел её источник — пена детальной сетки, и шлейф ручья продолжается в озеро
class WaterFoam
{
public:
	static constexpr float growthTime = 0.25f;	// с: пена у источника растёт к нему за это время (и пена окна у камеры)

	// Правила пены (WaterFoamBuffer, Shaders/water_foam.sh) по настройкам — у поля и у пены окна у камеры (NearWaterFlow);
	// шаг — timeStep, с
	static WaterFoamBuffer parameters( const WaterSimulationSettings& settings, float timeStep );

	bool initialize( WaterField& field );
	// Поле заново — по течению статичной воды и настройкам пены (время в лог); false — течения нет
	bool build( const StaticWater& water, const WaterSimulationSettings& settings );
	// Поле посчитано по тем же настройкам пены (parameters без шага)
	bool builtWith( const WaterSimulationSettings& settings ) const;
	bool ready() const { return m_ready; }
	// Доля пены: сетка 1 м (R16_FLOAT, строки — против Z мира) и детальная (массив по плиткам детальной воды, с полем)
	const ShaderView& coarseView() const { return m_coarse.result(); }
	const ShaderView& detailView() const { return m_detail.result(); }
	// В DDS R32_FLOAT: <prefix>_foam.dds — сетка 1 м, <prefix>_detail_foam.dds — детальная растром 0,25 м на весь мир (0 —
	// плитки нет); строка 0 — дальний край по z. Ждёт GPU
	bool exportState( const StaticWater& water, const std::string& prefix, std::string& reason );

private:
	// Источник и два поля по очереди (шаг читает одно, пишет другое)
	struct Grid
	{
		Texture textures[3];
		StorageView uavs[3];
		ShaderView views[3];
		uint32_t current = 1;	// где итог: 1 или 2 (0 — источник)
		const ShaderView& result() const { return views[current]; }
	};
	bool createGrid( Grid& grid, uint32_t width, uint32_t height, uint32_t slices, const char* name );

	WaterField* m_field = nullptr;
	ComputeShader m_detailSourceShader;
	ComputeShader m_detailStepShader;
	ComputeShader m_coarseSourceShader;
	ComputeShader m_coarseStepShader;
	Buffer m_constants;			// WaterFoamBuffer
	WaterGrid m_grid;
	Grid m_detail;
	Grid m_coarse;
	uint32_t m_detailSlices = 0;	// срезов детальных массивов (плиток при их создании)
	bool m_ready = false;
	// Правила пены, по которым посчитано поле (parameters с шагом 0)
	WaterFoamBuffer m_builtParameters = {};
};

}
