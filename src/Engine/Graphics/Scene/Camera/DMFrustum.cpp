#include "DMFrustum.h"


DMFrustum::DMFrustum(  )
{
}

DMFrustum::DMFrustum( const DMCamera& camera, float screenDepth )
{
	ConstructFrustum( camera, screenDepth );
}


DMFrustum::~DMFrustum()
{
}

void DMFrustum::ConstructFrustum( const DMCamera& camera, float screenDepth )
{
	XMMATRIX matrix;

	XMMATRIX projectionMatrix, viewMatrix;

	camera.projectionMatrix( &projectionMatrix );
	camera.viewMatrix( &viewMatrix );

	matrix = XMMatrixMultiply( viewMatrix, projectionMatrix );

	XMFLOAT4X4 m;
	XMStoreFloat4x4( &m, matrix );

	//right
	XMFLOAT4 plane;
	plane.x = m._14 - m._11;
	plane.y = m._24 - m._21;
	plane.z = m._34 - m._31;
	plane.w = m._44 - m._41;
	m_planes[0] = XMLoadFloat4( &plane );
	m_planes[0] = XMPlaneNormalize( m_planes[0] );

	//left
	plane.x = m._14 + m._11;
	plane.y = m._24 + m._21;
	plane.z = m._34 + m._31;
	plane.w = m._44 + m._41;
	m_planes[1] = XMLoadFloat4( &plane );
	m_planes[1] = XMPlaneNormalize( m_planes[1] );

	//bottom
	plane.x = m._14 + m._12;
	plane.y = m._24 + m._22;
	plane.z = m._34 + m._32;
	plane.w = m._44 + m._42;
	m_planes[2] = XMLoadFloat4( &plane );
	m_planes[2] = XMPlaneNormalize( m_planes[2] );

	//top
	plane.x = m._14 - m._12;
	plane.y = m._24 - m._22;
	plane.z = m._34 - m._32;
	plane.w = m._44 - m._42;
	m_planes[3] = XMLoadFloat4( &plane );
	m_planes[3] = XMPlaneNormalize( m_planes[3] );
	
	//far
	plane.x = m._14 - m._13;
	plane.y = m._24 - m._23;
	plane.z = m._34 - m._33;
	plane.w = m._44 - m._43;
	m_planes[4] = XMLoadFloat4( &plane );
	m_planes[4] = XMPlaneNormalize( m_planes[4] );

	// Calculate the minimum Z distance in the frustum.
	//zMinimum = -projectionMatrix._43 / projectionMatrix._33;
	//r = screenDepth / ( screenDepth - zMinimum );
	//projectionMatrix._33 = r;
	//projectionMatrix._43 = -r * zMinimum;

	matrix = XMMatrixMultiply( viewMatrix, projectionMatrix );
	XMStoreFloat4x4( &m, matrix );

	//near
	plane.x = m._14 + m._13;
	plane.y = m._24 + m._23;
	plane.z = m._34 + m._33;
	plane.w = m._44 + m._43;
	m_planes[5] = XMLoadFloat4( &plane );
	m_planes[5] = XMPlaneNormalize( m_planes[5] );

	
	

	
	/*float zMinimum, r;
	XMMATRIX matrix;

	XMMATRIX projectionMatrix, viewMatrix;

	camera.projectionMatrix( &projectionMatrix );
	camera.viewMatrix( &viewMatrix );


	// Calculate the minimum Z distance in the frustum.
	zMinimum = -projectionMatrix._43 / projectionMatrix._33;
	r = screenDepth / ( screenDepth - zMinimum );
	projectionMatrix._33 = r;
	projectionMatrix._43 = -r * zMinimum;

	// Create the frustum matrix from the view matrix and updated projection matrix.
	XMMatrixMultiply( &matrix, &viewMatrix, &projectionMatrix );

	// Calculate near plane of frustum.
	m_planes[0].a = matrix._14 + matrix._13;
	m_planes[0].b = matrix._24 + matrix._23;
	m_planes[0].c = matrix._34 + matrix._33;
	m_planes[0].d = matrix._44 + matrix._43;
	XMPlaneNormalize( &m_planes[0], &m_planes[0] );

	// Calculate far plane of frustum.
	m_planes[1].a = matrix._14 - matrix._13;
	m_planes[1].b = matrix._24 - matrix._23;
	m_planes[1].c = matrix._34 - matrix._33;
	m_planes[1].d = matrix._44 - matrix._43;
	XMPlaneNormalize( &m_planes[1], &m_planes[1] );

	// Calculate left plane of frustum.
	m_planes[2].a = matrix._14 + matrix._11;
	m_planes[2].b = matrix._24 + matrix._21;
	m_planes[2].c = matrix._34 + matrix._31;
	m_planes[2].d = matrix._44 + matrix._41;
	XMPlaneNormalize( &m_planes[2], &m_planes[2] );

	// Calculate right plane of frustum.
	m_planes[3].a = matrix._14 - matrix._11;
	m_planes[3].b = matrix._24 - matrix._21;
	m_planes[3].c = matrix._34 - matrix._31;
	m_planes[3].d = matrix._44 - matrix._41;
	XMPlaneNormalize( &m_planes[3], &m_planes[3] );

	// Calculate top plane of frustum.
	m_planes[4].a = matrix._14 - matrix._12;
	m_planes[4].b = matrix._24 - matrix._22;
	m_planes[4].c = matrix._34 - matrix._32;
	m_planes[4].d = matrix._44 - matrix._42;
	XMPlaneNormalize( &m_planes[4], &m_planes[4] );

	// Calculate bottom plane of frustum.
	m_planes[5].a = matrix._14 + matrix._12;
	m_planes[5].b = matrix._24 + matrix._22;
	m_planes[5].c = matrix._34 + matrix._32;
	m_planes[5].d = matrix._44 + matrix._42;
	XMPlaneNormalize( &m_planes[5], &m_planes[5] );
	*/
	return;
}

bool DMFrustum::CheckPoint( float x, float y, float z ) const
{
	// Check if the point is inside all six planes of the view frustum.
	for( uint8_t i = 0; i < 6; i++ )
	{	
		if( planeDot( i, x, y, z ) < 0.0f )
		{
			return false;
		}
	}

	return true;
}

float DMFrustum::planeDot( uint8_t index, float x, float y, float z ) const
{
	XMFLOAT3 point( x, y, z );
	XMVECTOR xmPoint = XMLoadFloat3( &point );
	XMVECTOR xmResult;
	XMFLOAT3 result;
	xmResult = XMPlaneDotCoord( m_planes[index], xmPoint );
	XMStoreFloat3( &result, xmResult );
	return result.x;
}

bool DMFrustum::CheckQuad( float xCenter, float yCenter, float zCenter, float radius ) const
{
	// Check if any one point of the cube is in the view frustum.
	for( uint8_t i = 0; i < 6; i++ )
	{
		if( planeDot( i, xCenter - radius, yCenter - 100.0f, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter - 100.0f, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - radius, yCenter + 100.0f, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter + 100.0f, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - radius, yCenter - 100.0f, zCenter + radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter - 100.0f, zCenter + radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - radius, yCenter + 100.0f, zCenter + radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter + 100.0f, zCenter + radius ) >= 0.0f ) continue;

		return false;
	}

	return true;
}

bool DMFrustum::CheckCube( float xCenter, float yCenter, float zCenter, float radius ) const
{
	// Check if any one point of the cube is in the view frustum.
	for( uint8_t i = 0; i < 6; i++ )
	{
		if( planeDot( i, xCenter - radius, yCenter - radius, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter - radius, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - radius, yCenter + radius, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter + radius, zCenter - radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - radius, yCenter - radius, zCenter + radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter - radius, zCenter + radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - radius, yCenter + radius, zCenter + radius ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + radius, yCenter + radius, zCenter + radius ) >= 0.0f ) continue;

		return false;
	}

	return true;
}

bool DMFrustum::CheckSphere( float xCenter, float yCenter, float zCenter, float radius ) const
{
	// Check if the radius of the sphere is inside the view frustum.
	for( uint8_t i = 0; i<6; i++ )
	{	
		if( planeDot( i, xCenter, yCenter, zCenter ) < -radius )
		{
			return false;
		}
	}

	return true;
}

bool DMFrustum::CheckAABB( DMAABB* aabb ) const
{
	XMFLOAT3 pos;
	XMStoreFloat3( &pos, aabb->position() );
	XMFLOAT3 size;
	XMStoreFloat3( &size, aabb->size() );
	
	return CheckRectangle( pos.x, pos.y, pos.z, size.x, size.y, size.z );
}

bool DMFrustum::CheckRectangle( float xCenter, float yCenter, float zCenter, float xSize, float ySize, float zSize ) const
{
	// Check if any of the 6 planes of the rectangle are inside the view frustum.
	for( uint8_t i = 0; i<6; i++ )
	{
		if( planeDot( i, xCenter - xSize, yCenter - ySize, zCenter - zSize ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + xSize, yCenter - ySize, zCenter - zSize ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - xSize, yCenter + ySize, zCenter - zSize ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - xSize, yCenter - ySize, zCenter + zSize ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + xSize, yCenter + ySize, zCenter - zSize ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + xSize, yCenter - ySize, zCenter + zSize ) >= 0.0f ) continue;
		if( planeDot( i, xCenter - xSize, yCenter + ySize, zCenter + zSize ) >= 0.0f ) continue;
		if( planeDot( i, xCenter + xSize, yCenter + ySize, zCenter + zSize ) >= 0.0f ) continue;

		return false;
	}

	return true;
}