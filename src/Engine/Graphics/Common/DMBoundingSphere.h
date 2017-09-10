#pragma once

#include <D3DX10math.h>

class DMBoundingSphere
{
public:
	DMBoundingSphere();
	~DMBoundingSphere();

	D3DXVECTOR3 m_center;
	float m_radius;
};

