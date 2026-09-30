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
	m_nearPlane = _near;
	m_farPlane = depth;
	m_fieldOfView = fieldOfView;
	setViewport( width, height );

	auto prop = m_properties.insert( "Camera speed", 1.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 100.0f );
	prop->setControlType( GUIControlType::SLIDER );
}

void DMCamera::setViewport( float width, float height )
{
	const float screenAspect = width / height;

	// Reversed-Z, как в UE: ближняя и дальняя плоскости переставлены — глубина 1 у ближней, 0 у дальней. Вместе с буфером
	// D32_FLOAT точность float и 1/z складываются, и шаг глубины почти не зависит от расстояния
	switch( m_type )
	{
		case DMCamera::CT_PERSPECTIVE:
			m_projection_matrix = XMMatrixPerspectiveFovLH( m_fieldOfView, screenAspect, m_farPlane, m_nearPlane );
			break;
		case DMCamera::CT_ORTHO:
			m_projection_matrix = XMMatrixOrthographicLH( width, height, m_farPlane, m_nearPlane );
			break;
		default:
			break;
	}
}

void DMCamera::projectionMatrix( XMMATRIX* matrix ) const
{
	*matrix = m_projection_matrix;
}

void DMCamera::SetPosition( float x, float y, float z )
{
	m_Eye.x = x;
	m_Eye.y = y;
	m_Eye.z = z;
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
	
	XMStoreFloat3( &m_view_direction, lookAtVector );

	return;
}

void DMCamera::setView( const XMFLOAT3& position, float pitch, float yaw )
{
	// В Update поворот — сумма заданного и накопленного мышью (0,1° на единицу), поэтому вычитаем накопленное
	constexpr float mouseForce = 0.1f;
	SetPosition( position.x, position.y, position.z );
	SetRotation( pitch - static_cast<float>( m_mouseY ) * mouseForce, yaw - static_cast<float>( m_mouseX ) * mouseForce, 0.0f );
}

XMFLOAT2 DMCamera::rotation() const
{
	constexpr float mouseForce = 0.1f;
	return XMFLOAT2( m_rotationX + static_cast<float>( m_mouseY ) * mouseForce, m_rotationY + static_cast<float>( m_mouseX ) * mouseForce );
}

const XMFLOAT3& DMCamera::position( ) const
{
	return m_Eye;
}

void DMCamera::position( XMFLOAT3* vec ) const
{
	*vec = m_Eye;
}

void DMCamera::readKeyboard( XMFLOAT3& offsetPosition )
{
	//update main camera position
	Input& input = getInput();

	float speedMultiplier = 0.1 * m_properties["Camera speed"].data<float>() ;

	if( input.IsForwarPressed() )
	{
		offsetPosition.z += 1.0f * speedMultiplier;
	}

	if( input.IsBackwardPressed() )
	{
		offsetPosition.z -= 1.0f * speedMultiplier;
	}

	if( input.IsRightStride() )
	{
		offsetPosition.x += 1.0f * speedMultiplier;
	}

	if( input.IsLeftStride() )
	{
		offsetPosition.x -= 1.0f * speedMultiplier;
	}

	if( input.IsUpMove() )
	{
		offsetPosition.y += 1.0f * speedMultiplier;
	}

	if( input.IsDownMove() )
	{
		offsetPosition.y -= 1.0f * speedMultiplier;
	}
}

void DMCamera::Update( float elapsedTime, bool cursorMode )
{
	XMMATRIX rotationMatrix;
	// Setup the vector that points upwards.
	XMFLOAT3 up( 0.0, 1.0, 0.0 );

	// Setup where the camera is looking by default.
	XMFLOAT3 lookAt( 0.0, 0.0, 1.0 );

	XMFLOAT3 posDirection( 0.0, 0.0, 0.0 );
	readKeyboard( posDirection );
	XMVECTOR vPosDelta = XMLoadFloat3( &posDirection );
	vPosDelta = XMVectorScale( vPosDelta, elapsedTime );
	vPosDelta = XMVectorScale( vPosDelta, 0.1f );

	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.
	if( !cursorMode )
	{
		getInput().GetMouseLocation( m_mouseX, m_mouseY );
		SetCursorPos( 600, 600 );
	}	

	float mouseForse = 0.1f;
	float pitch = ( m_rotationX + m_mouseY * mouseForse ) * 0.0174532925f;
	float yaw = ( m_rotationY + m_mouseX * mouseForse ) * 0.0174532925f;
	float roll = m_rotationZ * 0.0174532925f;

	// Create the rotation matrix from the yaw, pitch, and roll values.
	rotationMatrix = XMMatrixRotationRollPitchYaw( pitch, yaw, roll );

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	XMVECTOR lookAtVector = XMLoadFloat3( &lookAt );
	lookAtVector = XMVector3TransformCoord( lookAtVector, rotationMatrix );
	XMVECTOR viewDirection = XMVector3Normalize( lookAtVector );
	XMStoreFloat3( &m_view_direction, viewDirection );

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
	

	return;
}

void DMCamera::viewDirection( XMFLOAT3* vec ) const
{
	*vec = m_view_direction;
}

void DMCamera::viewMatrix( XMMATRIX* viewMatrix ) const
{
	memcpy( viewMatrix, &m_viewMatrix, sizeof( XMMATRIX ) ) ;
	return;
}
