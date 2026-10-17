#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <DirectXMath.h>

namespace GS
{

// Пересадка экземпляра модели на новую землю (ModelInstances::reseat, после эрозии): shift( x, z, radius ) — разница
// «самой высокой точки земли под моделью» между новым и прежним рельефом, м; радиус — по масштабу экземпляра
// (примитивы и тестовые модели — около метра), не меньше метра. Сдвиг меньше миллиметра — не двигаем. true — сдвинули
inline bool reseatPosition( DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& scale,
							const std::function<float( float x, float z, float radius )>& shift )
{
	const float radius = std::max( 0.5f * std::max( std::abs( scale.x ), std::abs( scale.z ) ), 1.0f );
	const float dy = shift( position.x, position.z, radius );
	if( std::abs( dy ) < 1e-3f )
		return false;
	position.y += dy;
	return true;
}

}
