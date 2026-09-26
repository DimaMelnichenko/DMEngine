
#include "DMTransformBuffer.h"


DMTransformBuffer::DMTransformBuffer(  ) : 
	m_position(0.0, 0.0, 0.0),
	m_scale( 1.0, 1.0, 1.0 )
{
	m_transform_link = nullptr;
	m_result_matrix = XMMatrixIdentity();
	m_position_matrix = XMMatrixIdentity();
	m_rotate_matrix = XMMatrixIdentity();
	m_scale_matrix = XMMatrixIdentity();
}

DMTransformBuffer::DMTransformBuffer( DMTransformBuffer&& source )
{
	*this = std::move( source );
}

DMTransformBuffer& DMTransformBuffer::operator=( DMTransformBuffer&& source )
{
	std::swap( m_result_matrix, source.m_result_matrix );
	std::swap( m_position_matrix, source.m_position_matrix );
	std::swap( m_rotate_matrix, source.m_rotate_matrix );
	std::swap( m_scale_matrix, source.m_scale_matrix );

	std::swap( m_aabb, source.m_aabb );

	std::swap( m_transform_link, source.m_transform_link );

	return *this;
}

DMTransformBuffer::DMTransformBuffer( const DMTransformBuffer& source )
{
	*this = source;
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
	
	memcpy( &this->m_result_matrix, &right.m_result_matrix, sizeof( XMMATRIX ) );	
	memcpy( &this->m_position_matrix, &right.m_position_matrix, sizeof( XMMATRIX ) );
	memcpy( &this->m_rotate_matrix, &right.m_rotate_matrix, sizeof( XMMATRIX ) );
	memcpy( &this->m_scale_matrix, &right.m_scale_matrix, sizeof( XMMATRIX ) );
	
	m_aabb = right.m_aabb;

    return *this;
}

void DMTransformBuffer::setPosition( const XMFLOAT3& vec )
{
	setPosition( vec.x, vec.y, vec.z );
}

const XMFLOAT3& DMTransformBuffer::scale() const
{
	return m_scale;
}

void DMTransformBuffer::setPosition( float x, float y, float z )
{
	m_position.x = x;
	m_position.y = y;
	m_position.z = z;


	m_position_matrix = XMMatrixTranslation( x, y, z );

	recalcMatrix();

	m_aabb.setPosition( x, y, z );
}

const XMFLOAT3& DMTransformBuffer::posf3() const
{
	return m_position;
}

XMVECTOR DMTransformBuffer::position() const
{
	return m_position_matrix.r[3];
}


const XMMATRIX& DMTransformBuffer::resultMatrix() const
{
	return m_result_matrix;
}

const XMMATRIX* DMTransformBuffer::resultMatrixPtr() const
{
	return &m_result_matrix;
}

void DMTransformBuffer::recalcMatrix()
{	
	m_result_matrix = XMMatrixMultiply( m_scale_matrix, m_rotate_matrix );
	m_result_matrix = XMMatrixMultiply( m_result_matrix, m_position_matrix );

	if( m_transform_link != nullptr )
	{
		m_result_matrix = XMMatrixMultiply( m_transform_link->resultMatrix(), m_result_matrix );
	}
}


void DMTransformBuffer::setScale( const XMFLOAT3& vec )
{
	m_scale = vec;
	XMMATRIX mat;

	m_scale_matrix = XMMatrixScaling( vec.x, vec.y, vec.z );

	recalcMatrix();

	m_aabb.setScale( vec );

}

