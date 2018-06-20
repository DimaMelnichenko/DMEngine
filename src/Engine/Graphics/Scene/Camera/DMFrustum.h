#pragma once

//////////////
// INCLUDES //
//////////////
#include "..\..\Common\DMAABB.h"
#include "DMCamera.h"


class DMFrustum
{
public:
	DMFrustum();
	DMFrustum( const DMCamera& camera, float screenDepth );
	~DMFrustum();

	void ConstructFrustum( const DMCamera& camera, float screenDepth );

	bool CheckPoint( float, float, float ) const;
	bool CheckQuad( float xCenter, float yCenter, float zCenter, float radius ) const;
	bool CheckCube( float, float, float, float ) const;
	bool CheckSphere( float, float, float, float ) const;
	bool CheckRectangle( float, float, float, float, float, float ) const;
	bool CheckAABB( DMAABB* ) const;

private:
	float planeDot( uint8_t index, float x, float y, float z ) const;
private:
	XMVECTOR m_planes[6];
};

