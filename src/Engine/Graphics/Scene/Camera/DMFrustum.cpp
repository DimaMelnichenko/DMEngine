#include "DMFrustum.h"

DMFrustum::DMFrustum( const XMMATRIX& viewProjection )
{
	XMFLOAT4X4 m;
	XMStoreFloat4x4( &m, viewProjection );

	// Плоскость — сумма или разность четвёртого столбца и столбца оси: right, left, bottom, top, затем z ≤ w и z ≥ 0
	// (в Direct3D глубина от 0, а не от −1, как в OpenGL). С обратной глубиной (Reversed-Z) z = w у ближней плоскости,
	// а z = 0 у дальней: последние две — ближняя и дальняя; отсечению порядок не важен
	const XMFLOAT4 planes[6] = {
		{ m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 },
		{ m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 },
		{ m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 },
		{ m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 },
		{ m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 },
		{ m._13, m._23, m._33, m._43 },
	};
	for( int i = 0; i < 6; ++i )
		m_planes[i] = XMPlaneNormalize( XMLoadFloat4( &planes[i] ) );
}

bool DMFrustum::checkBox( const XMFLOAT3& boxMin, const XMFLOAT3& boxMax ) const
{
	for( const XMVECTOR& plane : m_planes )
	{
		XMFLOAT4 p;
		XMStoreFloat4( &p, plane );

		// Угол параллелепипеда, дальше всех продвинутый вдоль нормали плоскости (нормали смотрят внутрь frustum)
		XMFLOAT3 corner( p.x >= 0.0f ? boxMax.x : boxMin.x,
						 p.y >= 0.0f ? boxMax.y : boxMin.y,
						 p.z >= 0.0f ? boxMax.z : boxMin.z );

		if( p.x * corner.x + p.y * corner.y + p.z * corner.z + p.w < 0.0f )
			return false;
	}

	return true;
}
