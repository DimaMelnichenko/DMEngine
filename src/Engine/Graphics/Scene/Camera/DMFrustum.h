#pragma once

#include "DirectX.h"

// Пирамида видимости: шесть плоскостей из матрицы вид × проекция (Gribb, Hartmann 2001). Строится для любого
// вида — главной камеры, позже каскада теней
class DMFrustum
{
public:
	explicit DMFrustum( const XMMATRIX& viewProjection = XMMatrixIdentity() );

	// Пересекает ли frustum (или содержит) ограничивающий параллелепипед, заданный углами в мировых координатах
	bool checkBox( const XMFLOAT3& boxMin, const XMFLOAT3& boxMax ) const;
	// Шесть нормированных плоскостей (a, b, c, d) с нормалями внутрь: точка p внутри, если a·x + b·y + c·z + d >= 0
	const XMVECTOR* planes() const { return m_planes; }

private:
	XMVECTOR m_planes[6];
};
