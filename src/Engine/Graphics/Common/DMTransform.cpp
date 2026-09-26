#include "DMTransform.h"


DMTransform::DMTransform() :
	m_worldMatrix( XMMatrixIdentity() )
{
}

void DMTransform::setPosition( const XMFLOAT3& position )
{
	m_position = position;
	recalcMatrix();
}

const XMFLOAT3& DMTransform::position() const
{
	return m_position;
}

void DMTransform::setRotation( const XMFLOAT4& rotation )
{
	XMVECTOR quaternion = XMLoadFloat4( &rotation );
	if( XMVectorGetX( XMVector4LengthSq( quaternion ) ) < 1e-12f )
		quaternion = XMQuaternionIdentity();
	XMStoreFloat4( &m_rotation, XMQuaternionNormalize( quaternion ) );
	recalcMatrix();
}

const XMFLOAT4& DMTransform::rotation() const
{
	return m_rotation;
}

void DMTransform::setScale( const XMFLOAT3& scale )
{
	m_scale = scale;
	recalcMatrix();
}

const XMFLOAT3& DMTransform::scale() const
{
	return m_scale;
}

const XMMATRIX& DMTransform::worldMatrix() const
{
	return m_worldMatrix;
}

void DMTransform::recalcMatrix()
{
	m_worldMatrix = XMMatrixScaling( m_scale.x, m_scale.y, m_scale.z ) *
					XMMatrixRotationQuaternion( XMLoadFloat4( &m_rotation ) ) *
					XMMatrixTranslation( m_position.x, m_position.y, m_position.z );
}
