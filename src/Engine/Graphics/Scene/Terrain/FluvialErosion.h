#pragma once

#include <cstdint>
#include <vector>
#include "DirectX.h"
#include "Level\LevelSettings.h"

namespace GS
{

// Речная эрозия на CPU (docs/far_terrain.md, «Речная и ледниковая эрозия») — закон stream power (врез ∝ A^m · S) неявной
// схемой FastScape (J. Braun, S. Willett 2013): устойчива при любом шаге, за шаг — один проход по сети стока. Сток — по
// залитой карте (priority-flood, R. Barnes 2014: из низин вода уходит через самую низкую седловину), к краю сетки и морю
// (ниже 0 м); водосбор — от верховий к устью. Реки прорезают древовидную сеть долин и острые гребни между ними, как
// Erosion в Gaea и World Machine; низины без стока спускаются врезом их порогов. Перед ледниковой эрозией (GlacialErosion):
// ледники идут по долинам, прорезанным реками
class FluvialErosion
{
public:
	// heights — карта на сетке size², м, строки с севера (меняется на месте); cell — шаг, м; origin — угол сетки с наименьшими
	// x, z мира; mapSize — сторона карты уровня (квадрат [0, mapSize]²: у неё эрозия гаснет)
	static void run( std::vector<float>& heights, uint32_t size, float cell, const DirectX::XMFLOAT2& origin, float mapSize,
					 const FluvialErosionSettings& settings );
};

}
