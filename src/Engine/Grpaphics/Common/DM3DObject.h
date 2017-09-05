#pragma once

#include "DMObject.h"
#include "DMD3D.h"
#include "utilites.h"

class DM3DObject :	public DMObject
{
public:
	DM3DObject( DMD3D* );
	~DM3DObject();

protected:
	DMD3D* m_dmd3d;
};

