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
	void CreateAABB( D3DXVECTOR3& position, D3DXVECTOR3& size );
	void setPosition( float x, float y, float z );
	void setScale( const D3DXVECTOR3& );
	void setScale( float x, float y, float z );
	void setSize( float x, float y, float z );
	const D3DXVECTOR3 position();
	const D3DXVECTOR3 size();

	DMAABB& operator=( const DMAABB& right );

private:
	D3DXVECTOR3 m_position;
	D3DXVECTOR3 m_base_position;
	D3DXVECTOR3 m_size;
	D3DXVECTOR3 m_min, m_max;	
};

