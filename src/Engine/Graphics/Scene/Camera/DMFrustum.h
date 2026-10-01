#pragma once

#include "DirectX.h"

// Пирамида видимости: шесть плоскостей из матрицы вид × проекция (Gribb, Hartmann 2001). Строится для любого
// вида — главной камеры, позже каскада теней
class DMFrustum
{
public:
	explicit DMFrustum( const DirectX::XMMATRIX& viewProjection = DirectX::XMMatrixIdentity() );

	// Пересекает ли frustum (или содержит) ограничивающий параллелепипед, заданный углами в мировых координатах
	bool checkBox( const DirectX::XMFLOAT3& boxMin, const DirectX::XMFLOAT3& boxMax ) const;
	// Шесть нормированных плоскостей (a, b, c, d) с нормалями внутрь: точка p внутри, если a·x + b·y + c·z + d >= 0
	const DirectX::XMVECTOR* planes() const { return m_planes; }

private:
	DirectX::XMVECTOR m_planes[6];
};
