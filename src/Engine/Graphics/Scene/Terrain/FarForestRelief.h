#pragma once

#include <cstdint>
#include <vector>

namespace GS
{

// Лавинные лотки (docs/far_terrain.md, «Лес на горах»): снег отрывается на открытых склонах круче slopeFrom (к slopeTo —
// полностью) выше height, идёт вниз по линии стока, пока склон не положе runout, и за ним быстро гаснет (зона выноса). Где по
// пути сошло с площади отрыва больше areaFrom гектаров (к areaTo — леса нет совсем), лес не растёт: просеки вниз по ложбинам
// под крутыми склонами, как в Альпах
struct AvalancheRules
{
	float slopeFrom = 30.0f;	// градусы
	float slopeTo = 40.0f;
	float height = 900.0f;		// м
	float runout = 15.0f;		// градусы
	float areaFrom = 3.0f;		// га
	float areaTo = 12.0f;
};

// Рельеф для леса дальних гор на сетке size², шаг cell, м, строки с севера (как карта гор): водосбор и лавинные лотки. Сток —
// по залитой карте (priority-flood, R. Barnes, C. Lehman, D. Mulla 2014: из низин через самую низкую седловину) к краю сетки,
// в соседа с наибольшим уклоном (D8); водосбор — от верховий к устью: порядок заливки наоборот (получатель ниже донора)
class FarForestRelief
{
public:
	// heights — size² высот, м
	void build( const std::vector<float>& heights, uint32_t size, float cell );
	uint32_t size() const { return m_size; }
	float cell() const { return m_cell; }
	// Водосбор клетки, м² (сама клетка и всё, что в неё стекает): у гребня — клетка, по дну долины — до сотен км²
	const std::vector<float>& drainage() const { return m_drainage; }
	// Доля лавинного лотка в клетке 0…1 по правилам — без новой заливки: правку окна видно сразу
	std::vector<float> avalanche( const AvalancheRules& rules ) const;

private:
	uint32_t m_size = 0;
	float m_cell = 0.0f;
	std::vector<float> m_heights;
	std::vector<float> m_slope;			// уклон, градусы
	std::vector<int32_t> m_receiver;	// куда стекает клетка; −1 — за край сетки
	std::vector<int32_t> m_order;		// порядок заливки: от края вверх (получатель раньше донора)
	std::vector<float> m_drainage;
};

}
