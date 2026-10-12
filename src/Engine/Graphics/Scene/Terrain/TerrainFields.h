#pragma once

#include <algorithm>
#include <cstdint>
#include <execution>
#include <numeric>
#include <vector>

namespace GS
{

// Общее для ступеней конвейера рельефа и воды, которые считают поля на CPU: правила слоёв (TerrainMasks) и поле ветра
// (TerrainWind). Поле — строки подряд, size × size; строка 0 — дальний край по z (+z мира — к меньшим строкам), столбец —
// по x
using TerrainField = std::vector<float>;

// Строки поля параллельно: fn( row )
template<typename Fn>
void forRows( uint32_t size, Fn fn )
{
	std::vector<uint32_t> rows( size );
	std::iota( rows.begin(), rows.end(), 0u );
	std::for_each( std::execution::par, rows.begin(), rows.end(), fn );
}

float fieldSmoothstep( float a, float b, float x );

// Три прохода box-фильтра по строкам и столбцам ≈ гауссово размытие; за краем карты повторяется крайний тексель
TerrainField blurField( TerrainField values, uint32_t size, int radius, int passes = 3 );

// Угол горизонта навстречу ветру Sx (Winstral 2002), градусы: наибольший угол подъёма рельефа на reach, м, против ветра,
// среднее по вееру ±15°, выборки с растущим шагом. > 0 — укрытие (против ветра выше), < 0 — открытый наветренный склон
// (рельеф против ветра уходит вниз), ровное место — около 0. heights × heightScale — метры; windX, windZ — куда дует ветер
// (X и Z мира); texel — шаг поля, м
TerrainField upwindHorizon( const TerrainField& heights, uint32_t size, float texel, float heightScale, float windX, float windZ,
							float reach );

}
