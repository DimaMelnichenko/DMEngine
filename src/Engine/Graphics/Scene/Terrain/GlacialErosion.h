#pragma once

#include <cstdint>
#include <vector>
#include "DirectX.h"
#include "Level\LevelSettings.h"

namespace GS
{

// Ледниковая эрозия на GPU (docs/far_terrain.md, «Ледниковая эрозия»): лёд ледникового периода как слой на сетке ложа —
// приближение мелкого льда (shallow ice approximation), как в моделях ландшафтной эволюции (J. Braun 1999; F. Herman,
// J. Braun 2008, ICE-CASCADE; D. Egholm 2011, iSOSIA): лёд течёт по уклону своей поверхности (закон Глена) и скользит по
// ложу, копится выше линии равновесия и тает ниже; ложе стирается пропорционально скорости скольжения (B. Harbor 1988).
// Выходят троги с U-профилем, кары и висячие долины. Сначала лёд набирается без эрозии, затем — эрозия, ускоренная:
// шаг стирает, как тысячи лет. Без лет эрозии — только лёд: так FarTerrain считает ледники наших дней. Шейдер —
// Shaders/glacial_erosion.cs; результат FarTerrain кэширует на диске
class GlacialErosion
{
public:
	struct Result
	{
		std::vector<float> bed;		// ложе после эрозии, м (size², строки с севера)
		std::vector<float> ice;		// толщина льда в конце, м
		std::vector<float> iceMax;	// наибольшая толщина льда за всё время, м
	};

	// bed — ложе на сетке size², м, строки с севера; cell — её шаг, м; origin — её угол с наименьшими x, z мира; mapSize —
	// сторона карты уровня (квадрат [0, mapSize]²: у неё эрозия гаснет); name — для строки в логе. Ждёт GPU. false — шейдер
	// или ресурсы не созданы
	static bool run( const std::vector<float>& bed, uint32_t size, float cell, const DirectX::XMFLOAT2& origin, float mapSize,
					 const GlacialErosionSettings& settings, const char* name, Result& result );
};

}
