#include "DMAABB.h"



DMAABB::DMAABB(  )
{
	CreateAABB( XMFLOAT3( 0.0, 0.5, 0.0 ), XMFLOAT3( 1.0, 0.5, 1.0 ) );
}

DMAABB::~DMAABB()
{
}

DMAABB& DMAABB::operator=( const DMAABB& right )
{
	//проверка на самоприсваивание
	if( this == &right )
	{
		return *this;
	}

	m_position = right.m_position;
	m_base_position = right.m_base_position;
	m_size = right.m_size;
	m_min = right.m_min;
	m_max = right.m_max;

	return *this;
}

void DMAABB::CreateAABB( XMFLOAT3& position, XMFLOAT3& size )
{
}

void DMAABB::setPosition( float x, float y, float z )
{

	m_min = XMVectorSubtract( m_position, m_size );
	m_max = XMVectorAdd( m_position, m_size );
 }

void DMAABB::setScale( const XMFLOAT3& vec )
{
	setScale( vec.x, vec.y, vec.z );
}

void DMAABB::setScale( float x, float y, float z )
{
	//
	//
}
