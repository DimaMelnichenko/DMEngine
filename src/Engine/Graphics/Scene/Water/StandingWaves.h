#pragma once

#include <algorithm>
#include <cmath>

namespace GS
{

// Стоячие волны над перекатами (docs/water.md, «Стоячие волны над перекатами»): волна стоит на месте, когда её скорость
// против течения равна скорости воды. Волновое число, 1/м, — из дисперсии волн на воде конечной глубины
// v² = g · th(k h) / k (Finch, GPU Gems 1, гл. 1), три шага; у почти критического течения решение уходит в длинные волны — не
// длиннее 2π глубины (k h ≥ 1); не короче standingMinWavelength. То же, что standingWavenumber в Shaders/water_standing.sh
constexpr float standingMinWavelength = 0.1f;	// м
constexpr float standingGravity = 9.81f;		// м/с²

inline float standingWavenumber( float speed, float depth )
{
	const float v2 = std::max( speed * speed, 1e-4f );
	const float h = std::max( depth, 0.01f );
	float k = standingGravity / v2;
	for( int i = 0; i < 3; ++i )
		k = std::max( standingGravity * std::tanh( k * h ) / v2, 1.0f / h );
	return std::min( k, 6.2831853f / standingMinWavelength );
}

}
