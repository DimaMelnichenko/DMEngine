#include "DMAABB.h"



DMAABB::DMAABB(  )
{
	CreateAABB( D3DXVECTOR3( 0.0, 0.5, 0.0 ), D3DXVECTOR3( 1.0, 0.5, 1.0 ) );
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

const bool DMAABB::OverlapsAABB( const DMAABB& aabb )
{
	const D3DXVECTOR3 t = aabb.m_position - m_position;
	return fabs( t.x ) <= ( m_size.x + aabb.m_size.x ) &&
		fabs( t.y ) <= ( m_size.y + aabb.m_size.y ) &&
		fabs( t.z ) <= ( m_size.z + aabb.m_size.z );
}

const bool DMAABB::OverlapsSphere( const DMBoundingSphere& sphere )
{
	float d = 0, a;

	// проходим по осям X,Y,Z
	for( int i = 0; i < 3; i++ )
	{
		// если центр сферы лежит перед AABB,
		if( sphere.m_center[i] < m_min[i] )
		{
			// то вычисляем квадрат расстояния по этой оси
			a = sphere.m_center[i] - m_min[i];
			d += a * a;
		}

		// если центр сферы лежит после AABB,
		if( sphere.m_center[i] > m_max[i] )
		{
			// то вычисляем квадрат расстояния по этой оси
			a = sphere.m_center[i] - m_max[i];
			d += a * a;
		}
	}

	return d <= ( sphere.m_radius * sphere.m_radius );
}

void DMAABB::CreateAABBFromVertex( void* data, size_t offset, size_t size )
{
	D3DXVECTOR3* vertex = reinterpret_cast<D3DXVECTOR3*>( data );
	m_min = m_max = *vertex;

	// ищем минимальную/максимальную точки
	for( size_t i = 0; i < size; i++ )
	{
		vertex = reinterpret_cast<D3DXVECTOR3*>( (char*)data + i * offset );
		// проходим по x,y,z
		
		m_max.x = max( m_max.x, vertex->x );
		m_max.y = max( m_max.y, vertex->y );
		m_max.z = max( m_max.z, vertex->z );
		m_min.x = min( m_min.x, vertex->x );
		m_min.y = min( m_min.y, vertex->y );
		m_min.z = min( m_min.z, vertex->z );
	}

	// вычисляем размеры
	m_size = ( m_max - m_min ) / 2.f;

	// и положение в пространстве
	m_base_position = m_min + m_size;
	m_position = m_base_position;
}

void DMAABB::CreateAABB( D3DXVECTOR3& position, D3DXVECTOR3& size )
{
	m_position = position;
	m_base_position = m_position;
	m_size = size;

	m_min = m_position - m_size;
	m_max = m_position + m_size;
}

const D3DXVECTOR3 DMAABB::position()
{
	return m_position;
}

const D3DXVECTOR3 DMAABB::size()
{
	return m_size;
}

void DMAABB::setSize( float x, float y, float z )
{
	m_size.x = x;
	m_size.y = y;
	m_size.z = z;

	m_min = m_position - m_size;
	m_max = m_position + m_size;
}

void DMAABB::setPosition( float x, float y, float z )
{
	m_position.x = m_base_position.x + x;
	m_position.y = m_base_position.y + y;
	m_position.z = m_base_position.z + z;

	m_min = m_position - m_size;
	m_max = m_position + m_size;
 }

void DMAABB::setScale( const D3DXVECTOR3& vec )
{
	setScale( vec.x, vec.y, vec.z );
}

void DMAABB::setScale( float x, float y, float z )
{
	m_size.x *= x;
	m_size.y *= y;
	m_size.z *= z;
	m_position.x *= x;
	m_position.y *= y;
	m_position.z *= z;

	m_base_position = m_position;

	m_min = m_position - m_size;
	m_max = m_position + m_size;
}
