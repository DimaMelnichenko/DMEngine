
#include "DMTransformBuffer.h"


DMTransformBuffer::DMTransformBuffer(  )
{
	m_transform_link = nullptr;
	D3DXMatrixIdentity( &m_result_matrix );	
	D3DXMatrixIdentity( &m_position_matrix );
	D3DXMatrixIdentity( &m_rotate_matrix );
	D3DXMatrixIdentity( &m_scale_matrix );
}

DMTransformBuffer::DMTransformBuffer( DMTransformBuffer&& source )
{
	std::swap( m_result_matrix, source.m_result_matrix );
	std::swap( m_position_matrix, source.m_position_matrix );
	std::swap( m_rotate_matrix, source.m_rotate_matrix );
	std::swap( m_scale_matrix, source.m_scale_matrix );

	std::swap( m_aabb, source.m_aabb );

	std::swap( m_transform_link, source.m_transform_link );
}

DMTransformBuffer::DMTransformBuffer( const DMTransformBuffer* source )
{
	this->m_result_matrix = source->m_result_matrix;
	this->m_position_matrix = source->m_position_matrix;
	this->m_rotate_matrix = source->m_rotate_matrix;
	this->m_scale_matrix = source->m_scale_matrix;

	m_aabb = source->m_aabb;

	m_transform_link = source->m_transform_link;
}

DMTransformBuffer::DMTransformBuffer( const DMTransformBuffer& source )
{
	this->m_result_matrix = source.m_result_matrix;
	this->m_position_matrix = source.m_position_matrix;
	this->m_rotate_matrix = source.m_rotate_matrix;
	this->m_scale_matrix = source.m_scale_matrix;

	m_aabb = source.m_aabb;

	m_transform_link = source.m_transform_link;
}


DMTransformBuffer::~DMTransformBuffer(void)
{

}

DMTransformBuffer& DMTransformBuffer::operator=(const DMTransformBuffer& right)
{
    //проверка на самоприсваивание
    if( this == &right )
	{
        return *this;
    }
	
	memcpy( &this->m_result_matrix, &right.m_result_matrix, sizeof( D3DXMATRIX ) );	
	memcpy( &this->m_position_matrix, &right.m_position_matrix, sizeof( D3DXMATRIX ) );
	memcpy( &this->m_rotate_matrix, &right.m_rotate_matrix, sizeof( D3DXMATRIX ) );
	memcpy( &this->m_scale_matrix, &right.m_scale_matrix, sizeof( D3DXMATRIX ) );
	
	m_aabb = right.m_aabb;

    return *this;
}

void DMTransformBuffer::setPosition( const D3DXVECTOR3& vec )
{
	setPosition( vec.x, vec.y, vec.z );
}

void DMTransformBuffer::setPosition( float x, float y, float z )
{
	D3DXMatrixTranslation( &m_position_matrix, x, y, z );

	recalcMatrix();

	m_aabb.setPosition( x, y, z );
}

void DMTransformBuffer::position( D3DXVECTOR3* pos )
{
	pos->x = m_position_matrix._41;
	pos->y = m_position_matrix._42;
	pos->z = m_position_matrix._43;
}

D3DXVECTOR3 DMTransformBuffer::position()
{
	return D3DXVECTOR3( m_position_matrix._41, m_position_matrix._42, m_position_matrix._43 );
}

void DMTransformBuffer::setRotationAxis( const D3DXVECTOR3& vec, float degree )
{
	D3DXMatrixRotationAxis( &m_rotate_matrix, &vec, degree );

	recalcMatrix();
}

void DMTransformBuffer::setRotationAxis( float x, float y, float z, float degree )
{
	static D3DXVECTOR3 rot;
	rot.x = x;
	rot.y = y;
	rot.z = z;
	setRotationAxis( rot, degree );
}


void DMTransformBuffer::resultMatrix( D3DXMATRIX* result )
{
	memcpy( result, &m_result_matrix, sizeof( D3DXMATRIX ) );
}

D3DXMATRIX DMTransformBuffer::resultMatrix()
{
	return m_result_matrix;
}


void DMTransformBuffer::recalcMatrix()
{	
	D3DXMatrixMultiply( &m_result_matrix, &m_scale_matrix, &m_rotate_matrix );
	D3DXMatrixMultiply( &m_result_matrix, &m_result_matrix, &m_position_matrix );

	if( m_transform_link )
	{
		D3DXMatrixMultiply( &m_result_matrix, &m_transform_link->resultMatrix(), &m_result_matrix );
	}
}


void DMTransformBuffer::setScale( const D3DXVECTOR3& vec )
{
	D3DXMATRIX mat;

	D3DXMatrixScaling( &m_scale_matrix, vec.x, vec.y, vec.z );

	recalcMatrix();

	m_aabb.setScale( vec );

}

void DMTransformBuffer::setScale( float x )
{
	setScale( D3DXVECTOR3( x, x, x )  );
}

void DMTransformBuffer::setLink( DMTransformBuffer* dest )
{
	m_transform_link = dest;
}

void DMTransformBuffer::unlink()
{
	m_transform_link = nullptr;
}


DMAABB& DMTransformBuffer::aabb()
{
	return m_aabb;
}