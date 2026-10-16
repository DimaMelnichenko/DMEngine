#pragma once

#include <vector>
#include "WaterField.h"
#include "StaticWater.h"
#include "D3D\GpuPass.h"
#include "Shaders\water_grid_constants.h"

namespace GS
{

// Счёт по плиткам детальной воды и сетке 1 м при загрузке (Shaders/water_detail_grid.sh): поле пены (WaterFoam) и фаза
// стоячих волн (StandingWaves). Константы сеток (b6) и общие ресурсы прохода — детальная вода, индекс и координаты плиток,
// группы, вода сетки 1 м (слоты 0–4)
class WaterGrid
{
public:
	bool initialize();
	// Сетки статичной воды: детальная — если есть плитки, блоки 8 × 8 с водой и индекс плиток детальной земли
	void prepare( const WaterField& field, const StaticWater& water );
	bool detail() const { return m_detail; }
	// Проход над группами: блоками детальной воды (detailGroups) или тайлами сетки 1 м; reads и writes — свои ресурсы прохода,
	// общие добавляются; после — константы сетки в b6 и общие ресурсы в слоты 0–4
	void beginPass( const char* name, bool detailGroups, std::vector<PassDesc::Read> reads, std::vector<PassDesc::Write> writes );
	uint32_t groupCount( bool detailGroups ) const;

private:
	const WaterField* m_field = nullptr;
	const StaticWater* m_water = nullptr;
	TerrainHeight m_height;
	bool m_detail = false;
	Buffer m_constants;		// WaterGridBuffer
};

}
