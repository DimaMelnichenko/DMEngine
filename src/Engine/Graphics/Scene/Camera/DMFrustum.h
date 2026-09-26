#pragma once

//////////////
// INCLUDES //
//////////////
#include "..\..\Common\DMAABB.h"
#include "DMCamera.h"


class DMFrustum
{
public:
	DMFrustum( const DMCamera& camera, float screenDepth );
	~DMFrustum();

	void ConstructFrustum( const DMCamera& camera, float screenDepth );

	// Пересекает ли frustum (или содержит) ограничивающий параллелепипед, заданный углами в мировых координатах
	bool checkBox( const XMFLOAT3& boxMin, const XMFLOAT3& boxMax ) const;
	// Шесть нормированных плоскостей (a, b, c, d) с нормалями внутрь: точка p внутри, если a·x + b·y + c·z + d >= 0
	const XMVECTOR* planes() const { return m_planes; }

private:
	XMVECTOR m_planes[6];
};

