#pragma once

#include "DirectX.h"

class DMAABB
{
public:
	DMAABB();
	~DMAABB();
	void CreateAABB( XMFLOAT3& position, XMFLOAT3& size );
	void setPosition( float x, float y, float z );
	void setScale( const XMFLOAT3& );
	void setScale( float x, float y, float z );

	DMAABB& operator=( const DMAABB& right );

private:
	XMVECTOR m_position;
	XMVECTOR m_base_position;
	XMVECTOR m_size;
	XMVECTOR m_min, m_max;	
};

