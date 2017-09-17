#pragma once

//////////////
// INCLUDES //
//////////////
#include "..\..\Common\DMAABB.h"
#include "DMCamera.h"


class DMFrustum
{
public:
	DMFrustum(  );
	~DMFrustum();

	void ConstructFrustum( DMCamera* camera, float screenDepth );

	bool CheckPoint( float, float, float );
	bool CheckQuad( float xCenter, float yCenter, float zCenter, float radius );
	bool CheckCube( float, float, float, float );
	bool CheckSphere( float, float, float, float );
	bool CheckRectangle( float, float, float, float, float, float );
	bool CheckAABB( DMAABB* );

private:
	D3DXPLANE m_planes[6];
};

