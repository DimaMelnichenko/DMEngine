#pragma once

#include "DMBoundingSphere.h"

class DMAABB
{
public:
	DMAABB();
	~DMAABB();

	const bool OverlapsAABB( const DMAABB& aabb );
	const bool OverlapsSphere( const DMBoundingSphere& sphere );
	void CreateAABBFromVertex( void* data, size_t offset, size_t size );
	void CreateAABB( XMFLOAT3& position, XMFLOAT3& size );
	void setPosition( float x, float y, float z );
	void setScale( const XMFLOAT3& );
	void setScale( float x, float y, float z );
	void setSize( float x, float y, float z );
	const XMVECTOR position();
	const XMVECTOR size();

	DMAABB& operator=( const DMAABB& right );

private:
	XMVECTOR m_position;
	XMVECTOR m_base_position;
	XMVECTOR m_size;
	XMVECTOR m_min, m_max;	
};

