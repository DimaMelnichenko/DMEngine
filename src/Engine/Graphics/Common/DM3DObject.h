#pragma once

#include "..\D3D\DMD3D.h"
#include "..\..\..\Utils\utilites.h"

class DM3DObject
{
public:
	DM3DObject( DMD3D* );
	~DM3DObject();

protected:
	DMD3D* m_dmd3d;
};

