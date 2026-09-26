#include "DMFrustum.h"

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

	matrix = XMMatrixMultiply( viewMatrix, projectionMatrix );
	XMStoreFloat4x4( &m, matrix );

	//near
	plane.x = m._14 + m._13;
	plane.y = m._24 + m._23;
	plane.z = m._34 + m._33;
	plane.w = m._44 + m._43;
	m_planes[5] = XMLoadFloat4( &plane );
	m_planes[5] = XMPlaneNormalize( m_planes[5] );





	return;
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
