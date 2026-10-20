#pragma once

#include <cstdint>
#include <vector>
#include <DirectXMath.h>

namespace GS
{

// Верх камня сверху (docs/water.md, «Подробное течение у камеры»): квадрат cells × cells текселей над камнем в мире, в
// текселе — наибольшая высота поверхности меша над его центром, м над низом камня (noTop — меша над текселем нет). Его
// берёт окно подробного течения у камеры — препятствие по форме модели камня, а не полуэллипсоид по поперечнику
struct StoneTop
{
	static constexpr uint32_t cells = 16;		// текселей по стороне (WATER_NEAR_STONE_CELLS в шейдере окна)
	static constexpr float noTop = -1.0f;

	DirectX::XMFLOAT2 origin = DirectX::XMFLOAT2( 0.0f, 0.0f );	// угол квадрата (наименьшие x и z), м мира
	float side = 0.0f;			// сторона квадрата, м: наибольший размах меша по x и z
	float base = 0.0f;			// низ меша, м мира: от него отсчитан верх
	std::vector<float> heights;	// cells², строка — по z (тексель (i, j) — центр origin + ( (i, j) + ½ ) · side / cells)
};

// Верх меша (points, тройки indices) с мировой матрицей world: треугольники растрируются сверху по центрам текселей,
// высота — по барицентрическим координатам, в текселе — наибольшая. Пустой меш — side 0
StoneTop rasterizeStoneTop( const std::vector<DirectX::XMFLOAT3>& points, const std::vector<uint32_t>& indices,
							DirectX::FXMMATRIX world );

}
