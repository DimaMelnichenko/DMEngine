#pragma once

#include "DirectX.h"

class DMBoundingSphere
{
public:
	DMBoundingSphere();
	~DMBoundingSphere();

	D3DXVECTOR3 m_center;
	float m_radius;
};

