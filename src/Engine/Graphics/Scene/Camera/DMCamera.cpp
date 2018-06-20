#include "DMCamera.h"
#include "Engine\Input\Input.h"

DMCamera::DMCamera(  ) :
	m_Eye( XMFLOAT3( 0.0, 0.0, 0.0 ) )
{	
	m_rotationX = 0.0f;
	m_rotationY = 0.0f;
	m_rotationZ = 0.0f;

	m_view_direction = XMFLOAT3( 0.0, 0.0, 1.0 );
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
			m_projection_matrix = XMMatrixPerspectiveFovLH( fieldOfView, screenAspect, _near, depth );
			break;
		case DMCamera::CT_ORTHO:
			m_projection_matrix = XMMatrixOrthographicLH( (float)width, (float)height, _near, depth );
			break;
		default:
			break;
	}
}

void DMCamera::projectionMatrix( XMMATRIX* matrix ) const
{
	*matrix = m_projection_matrix;
}

const XMMATRIX& DMCamera::projectionMatrix() const
{
	return m_projection_matrix;
}

void DMCamera::SetPosition( float x, float y, float z )
{
	m_Eye.x = x;
	m_Eye.y = y;
	m_Eye.z = z;
	return;
}

void DMCamera::SetPosition( const XMFLOAT3 & vec )
{
	m_Eye.x = vec.x;
	m_Eye.y = vec.y;
	m_Eye.z = vec.z;
	return;
}

void DMCamera::SetRotation( float x, float y, float z )
{
	m_rotationX = x;
	m_rotationY = y;
	m_rotationZ = z;

	XMFLOAT3 lookAt;
	// Setup where the camera is looking by default.
	lookAt.x = 0.0f;
	lookAt.y = 0.0f;
	lookAt.z = 1.0f;

	float yaw, pitch, roll;
	XMMATRIX rotationMatrix;
	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.
	pitch	= static_cast<float>( m_rotationX * 0.0174532925 );
	yaw		= static_cast<float>( m_rotationY * 0.0174532925 );
	roll	= static_cast<float>( m_rotationZ * 0.0174532925 );

	// Create the rotation matrix from the yaw, pitch, and roll values.
	rotationMatrix = XMMatrixRotationRollPitchYaw( roll, pitch, yaw );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	XMVECTOR lookAtVector;
	lookAtVector = XMLoadFloat3( &lookAt );
	lookAtVector = XMVector3TransformCoord( lookAtVector, rotationMatrix );
	//XMVector3TransformCoord( &up, &up, &rotationMatrix );
	
	XMStoreFloat3( &m_view_direction, lookAtVector );

	return;
}

const XMFLOAT3& DMCamera::position( ) const
{
	return m_Eye;
}

void DMCamera::position( XMFLOAT3* vec ) const
{
	*vec = m_Eye;
}

XMFLOAT3 DMCamera::GetRotation( ) const
{
	return XMFLOAT3( m_rotationX, m_rotationY, m_rotationZ );
}

void DMCamera::readKeyboard( XMFLOAT3& offsetPosition )
{
	//update main camera position
	Input& input = getInput();

	if( input.IsForwarPressed() )
	{
		offsetPosition.z += 1.0f;
	}

	if( input.IsBackwardPressed() )
	{
		offsetPosition.z -= 1.0f;
	}

	if( input.IsRightStride() )
	{
		offsetPosition.x += 1.0f;
	}

	if( input.IsLeftStride() )
	{
		offsetPosition.x -= 1.0f;
	}

	if( input.IsUpMove() )
	{
		offsetPosition.y += 1.0f;
	}

	if( input.IsDownMove() )
	{
		offsetPosition.y -= 1.0f;
	}
}

void DMCamera::Update( float elapsedTime )
{	
	XMMATRIX rotationMatrix;
	// Setup the vector that points upwards.
	XMFLOAT3 up( 0.0, 1.0, 0.0 );

	// Setup where the camera is looking by default.
	XMFLOAT3 lookAt( 0.0, 0.0, 1.0 );

	XMFLOAT3 posDirection(0.0, 0.0, 0.0);
	readKeyboard( posDirection );
	XMVECTOR vPosDelta = XMLoadFloat3( &posDirection );
	vPosDelta = XMVectorScale( vPosDelta, elapsedTime );
	vPosDelta = XMVectorScale( vPosDelta, 0.01f );

	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.
	double mouseX;
	double mouseY;
	getInput().GetMouseLocation( mouseX, mouseY );

	float mouseForse = 0.1f;
	float pitch = ( m_rotationX + mouseY * mouseForse ) * 0.0174532925f;
	float yaw = ( m_rotationY + mouseX * mouseForse ) * 0.0174532925f;
	float roll = m_rotationZ * 0.0174532925f;

	// Create the rotation matrix from the yaw, pitch, and roll values.
	rotationMatrix = XMMatrixRotationRollPitchYaw( pitch, yaw, roll );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	XMVECTOR lookAtVector = XMLoadFloat3( &lookAt );
	lookAtVector = XMVector3TransformCoord( lookAtVector, rotationMatrix );
	XMVECTOR upVector = XMLoadFloat3( &up );
	upVector = XMVector3TransformCoord( upVector, rotationMatrix );

	vPosDelta = XMVector3TransformCoord( vPosDelta, rotationMatrix );
	XMVECTOR eyeVector = XMLoadFloat3( &m_Eye );
	eyeVector = XMVectorAdd( eyeVector, vPosDelta );

	// Translate the rotated camera position to the location of the viewer.
	lookAtVector = XMVectorAdd( eyeVector, lookAtVector );

	// Finally create the view matrix from the three updated vectors.
	m_viewMatrix = XMMatrixLookAtLH( eyeVector, lookAtVector, upVector );

	m_mCameraWorld = XMMatrixInverse( nullptr, m_viewMatrix );

	XMStoreFloat3( &m_Eye, eyeVector );
	XMStoreFloat3( &m_view_direction, lookAtVector );

	return;
}

void DMCamera::viewDirection( XMFLOAT3* vec ) const
{
	*vec = m_view_direction;
}

void DMCamera::SetDirection( const XMFLOAT3& vec )
{
	memcpy( &m_view_direction, &vec, sizeof( XMFLOAT3 ) );

	//XMMatrixRotationRollPitchYaw( &rotationMatrix, yaw, pitch, roll );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	//XMVector3TransformCoord( &lookAt, &lookAt, &rotationMatrix );
}

void DMCamera::SetDirection( float x, float y, float z )
{
	m_view_direction.x = x;
	m_view_direction.y = y;
	m_view_direction.z = z;
}

void DMCamera::viewMatrix( XMMATRIX* viewMatrix ) const
{
	memcpy( viewMatrix, &m_viewMatrix, sizeof( XMMATRIX ) ) ;
	return;
}

void DMCamera::RenderReflection( float height )
{
	XMFLOAT3 up, position, lookAt;
	float yaw, pitch, roll;
	XMMATRIX rotationMatrix;


	// Setup the vector that points upwards.
	up.x = 0.0f;
	up.y = 1.0f;
	up.z = 0.0f;

	// Setup the position of the camera in the world.  For planar reflection invert the Y position of the camera.
	position.x = m_Eye.x;
	position.y = -m_Eye.y + ( height * 2.0f );
	position.z = m_Eye.z;

	// Setup where the camera is looking by default.
	lookAt.x = 0.0f;
	lookAt.y = 0.0f;
	lookAt.z = 1.0f;

	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.  Invert the X rotation for reflection.
	pitch = -m_rotationX * 0.0174532925f;
	yaw = m_rotationY * 0.0174532925f;
	roll = m_rotationZ * 0.0174532925f;

	// Create the rotation matrix from the yaw, pitch, and roll values.
	rotationMatrix = XMMatrixRotationRollPitchYaw( roll, pitch, yaw );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	XMVECTOR lookAtVector = XMLoadFloat3( &lookAt );
	lookAtVector = XMVector3TransformCoord( lookAtVector, rotationMatrix );
	XMVECTOR upVector = XMLoadFloat3( &up );
	upVector = XMVector3TransformCoord( upVector, rotationMatrix );

	// Translate the rotated camera position to the location of the viewer.
	XMVECTOR eyeVector = XMLoadFloat3( &position );
	eyeVector = XMVectorAdd( eyeVector, lookAtVector );

	// Finally create the reflection view matrix from the three updated vectors.
	m_reflectionViewMatrix = XMMatrixLookAtLH( eyeVector, lookAtVector, upVector );

	return;
}

void DMCamera::GetReflectionViewMatrix( XMMATRIX& viewMatrix ) const
{
	viewMatrix = m_reflectionViewMatrix;
	return;
}

const XMMATRIX& DMCamera::viewMatrix() const
{
	return m_viewMatrix;
}

DMCamera::CameraType DMCamera::type() const
{
	return m_type;
}