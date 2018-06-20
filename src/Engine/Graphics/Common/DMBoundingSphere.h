#pragma once

#include "DirectX.h"

class DMBoundingSphere
{
public:
	DMBoundingSphere();
	~DMBoundingSphere();

	XMFLOAT3 m_center;
	float m_radius;
};

