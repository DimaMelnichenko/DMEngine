#include "DMCamera.h"


DMCamera::DMCamera(  )
{
	m_positionX = 0.0f;
	m_positionY = 0.0f;
	m_positionZ = 0.0f;

	m_rotationX = 0.0f;
	m_rotationY = 0.0f;
	m_rotationZ = 0.0f;

	m_view_direction = D3DXVECTOR3( 0.0, 0.0, 1.0 );
}


DMCamera::~DMCamera()
{
}

void DMCamera::Initialize( CameraType _type, float width, float height, float _near, float depth, float fieldOfView )
{
	m_type = _type;

	// Setup the projection matrix.
	float screenAspect = (float)width / (float)height;

	switch( _type )
	{
		case DMCamera::CT_PERSPECTIVE:
			D3DXMatrixPerspectiveFovLH( &m_projection_matrix, fieldOfView, screenAspect, _near, depth );
			break;
		case DMCamera::CT_ORTHO:
			D3DXMatrixOrthoLH( &m_projection_matrix, (float)width, (float)height, _near, depth );
			break;
		default:
			break;
	}
	
}

void DMCamera::projectionMatrix( D3DXMATRIX* _matrix )
{
	memcpy( _matrix, &m_projection_matrix, sizeof( D3DXMATRIX ) );
}

D3DXMATRIX DMCamera::projectionMatrix()
{
	return m_projection_matrix;
}

void DMCamera::SetPosition( float x, float y, float z )
{
	m_positionX = x;
	m_positionY = y;
	m_positionZ = z;
	return;
}

void DMCamera::SetPosition( const D3DXVECTOR3 & vec )
{
	m_positionX = vec.x;
	m_positionY = vec.y;
	m_positionZ = vec.z;
	return;
}

void DMCamera::SetRotation( float x, float y, float z )
{
	m_rotationX = x;
	m_rotationY = y;
	m_rotationZ = z;

	D3DXVECTOR3 lookAt;
	// Setup where the camera is looking by default.
	lookAt.x = 0.0f;
	lookAt.y = 0.0f;
	lookAt.z = 1.0f;

	float yaw, pitch, roll;
	D3DXMATRIX rotationMatrix;
	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.
	pitch	= static_cast<float>( m_rotationX * 0.0174532925 );
	yaw		= static_cast<float>( m_rotationY * 0.0174532925 );
	roll	= static_cast<float>( m_rotationZ * 0.0174532925 );

	// Create the rotation matrix from the yaw, pitch, and roll values.
	D3DXMatrixRotationYawPitchRoll( &rotationMatrix, yaw, pitch, roll );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	D3DXVec3TransformCoord( &lookAt, &lookAt, &rotationMatrix );
	//D3DXVec3TransformCoord( &up, &up, &rotationMatrix );

	m_view_direction = lookAt;

	return;
}

D3DXVECTOR3 DMCamera::position( )
{
	return D3DXVECTOR3( m_positionX, m_positionY, m_positionZ );
}

void DMCamera::position( D3DXVECTOR3* _vec )
{
	_vec->x = m_positionX;
	_vec->y = m_positionY;
	_vec->z = m_positionZ;
}

D3DXVECTOR3 DMCamera::GetRotation( )
{
	return D3DXVECTOR3( m_rotationX, m_rotationY, m_rotationZ );
}

void DMCamera::Render( )
{
	D3DXVECTOR3 up, position, lookAt;	
	D3DXMATRIX rotationMatrix;


	// Setup the vector that points upwards.
	up.x = 0.0f;
	up.y = 1.0f;
	up.z = 0.0f;
	
	// Setup the position of the camera in the world.
	position.x = m_positionX;
	position.y = m_positionY;
	position.z = m_positionZ;
	/*
	// Setup where the camera is looking by default.
	lookAt.x = 0.0f;
	lookAt.y = 0.0f;
	lookAt.z = 1.0f;

	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.
	pitch = m_rotationX * 0.0174532925f;
	yaw = m_rotationY * 0.0174532925f;
	roll = m_rotationZ * 0.0174532925f;

	// Create the rotation matrix from the yaw, pitch, and roll values.
	D3DXMatrixRotationYawPitchRoll( &rotationMatrix, yaw, pitch, roll );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	D3DXVec3TransformCoord( &lookAt, &lookAt, &rotationMatrix );
	//D3DXVec3TransformCoord( &up, &up, &rotationMatrix );

	m_view_direction = lookAt;
	*/
	// Translate the rotated camera position to the location of the viewer.
	lookAt = position + m_view_direction;

	// Finally create the view matrix from the three updated vectors.
	D3DXMatrixLookAtLH( &m_viewMatrix, &position, &lookAt, &up );

	return;
}

void DMCamera::viewDirection( D3DXVECTOR3* vec )
{
	memcpy( vec, &m_view_direction, sizeof( D3DXVECTOR3 ) );
}

void DMCamera::SetDirection( const D3DXVECTOR3& vec )
{
	memcpy( &m_view_direction, &vec, sizeof( D3DXVECTOR3 ) );

	//D3DXMatrixRotationYawPitchRoll( &rotationMatrix, yaw, pitch, roll );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	//D3DXVec3TransformCoord( &lookAt, &lookAt, &rotationMatrix );
}

void DMCamera::SetDirection( float x, float y, float z )
{
	m_view_direction.x = x;
	m_view_direction.y = y;
	m_view_direction.z = z;
}

void DMCamera::viewMatrix( D3DXMATRIX* viewMatrix )
{
	memcpy( viewMatrix, &m_viewMatrix, sizeof( D3DXMATRIX ) ) ;
	return;
}

void DMCamera::RenderReflection( float height )
{
	D3DXVECTOR3 up, position, lookAt;
	float yaw, pitch, roll;
	D3DXMATRIX rotationMatrix;


	// Setup the vector that points upwards.
	up.x = 0.0f;
	up.y = 1.0f;
	up.z = 0.0f;

	// Setup the position of the camera in the world.  For planar reflection invert the Y position of the camera.
	position.x = m_positionX;
	position.y = -m_positionY + ( height * 2.0f );
	position.z = m_positionZ;

	// Setup where the camera is looking by default.
	lookAt.x = 0.0f;
	lookAt.y = 0.0f;
	lookAt.z = 1.0f;

	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.  Invert the X rotation for reflection.
	pitch = -m_rotationX * 0.0174532925f;
	yaw = m_rotationY * 0.0174532925f;
	roll = m_rotationZ * 0.0174532925f;

	// Create the rotation matrix from the yaw, pitch, and roll values.
	D3DXMatrixRotationYawPitchRoll( &rotationMatrix, yaw, pitch, roll );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	D3DXVec3TransformCoord( &lookAt, &lookAt, &rotationMatrix );
	D3DXVec3TransformCoord( &up, &up, &rotationMatrix );

	// Translate the rotated camera position to the location of the viewer.
	lookAt = position + lookAt;

	// Finally create the reflection view matrix from the three updated vectors.
	D3DXMatrixLookAtLH( &m_reflectionViewMatrix, &position, &lookAt, &up );

	return;
}

void DMCamera::GetReflectionViewMatrix( D3DXMATRIX& viewMatrix )
{
	viewMatrix = m_reflectionViewMatrix;
	return;
}

D3DXMATRIX DMCamera::viewMatrix()
{
	return m_viewMatrix;
}

DMCamera::CameraType DMCamera::type()
{
	return m_type;
}