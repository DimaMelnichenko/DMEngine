#include "DMPosition.h"

DMPosition::DMPosition(  )
{
	m_positionX = 0.0f;
	m_positionY = 0.0f;
	m_positionZ = 0.0f;

	m_rotationX = 0.0f;
	m_rotationY = 0.0f;
	m_rotationZ = 0.0f;

	m_frameTime = 0.0f;

	m_forwardSpeed = 0.0f;
	m_backwardSpeed = 0.0f;
	m_upwardSpeed = 0.0f;
	m_downwardSpeed = 0.0f;
	m_leftwardSpeed = 0.0f;
	m_rightwardSpeed = 0.0f;
	m_leftTurnSpeed = 0.0f;
	m_rightTurnSpeed = 0.0f;
	m_lookUpSpeed = 0.0f;
	m_lookDownSpeed = 0.0f;
}

DMPosition::~DMPosition()
{
}

void DMPosition::SetPosition( float x, float y, float z )
{
	m_positionX = x;
	m_positionY = y;
	m_positionZ = z;
	return;
}

void DMPosition::SetRotation( float x, float y, float z )
{
	m_rotationX = x;
	m_rotationY = y;
	m_rotationZ = z;
	return;
}

void DMPosition::GetPosition( float& x, float& y, float& z )
{
	x = m_positionX;
	y = m_positionY;
	z = m_positionZ;
	return;
}

void DMPosition::GetRotation( float& x, float& y, float& z )
{
	x = m_rotationX;
	y = m_rotationY;
	z = m_rotationZ;
	return;
}

void DMPosition::SetFrameTime( float time )
{
	m_frameTime = time;
	return;
}

void DMPosition::MoveForward( bool keydown )
{
	float radians;


	// Update the forward speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_forwardSpeed += m_frameTime * 0.0001f;

		if( m_forwardSpeed > ( m_frameTime * 0.003f ) )
		{
			m_forwardSpeed = m_frameTime * 0.003f;
		}
	}
	else
	{
		m_forwardSpeed -= m_frameTime * 0.00007f;

		if( m_forwardSpeed < 0.0f )
		{
			m_forwardSpeed = 0.0f;
		}
	}

	// Convert degrees to radians.
	radians = m_rotationY * 0.0174532925f;

	// Update the position.
	m_positionX += sinf( radians ) * m_forwardSpeed;
	m_positionZ += cosf( radians ) * m_forwardSpeed;
	m_positionY -= sinf( m_rotationX * 0.0174532925f ) * m_forwardSpeed;

	return;
}

void DMPosition::MoveBackward( bool keydown )
{
	float radians;


	// Update the backward speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_backwardSpeed += m_frameTime * 0.0001f;

		if( m_backwardSpeed > ( m_frameTime * 0.003f ) )
		{
			m_backwardSpeed = m_frameTime * 0.003f;
		}
	}
	else
	{
		m_backwardSpeed -= m_frameTime * 0.00007f;

		if( m_backwardSpeed < 0.0f )
		{
			m_backwardSpeed = 0.0f;
		}
	}

	// Convert degrees to radians.
	radians = m_rotationY * 0.0174532925f;

	// Update the position.
	m_positionX -= sinf( radians ) * m_backwardSpeed;
	m_positionZ -= cosf( radians ) * m_backwardSpeed;
	m_positionY += sinf( m_rotationX * 0.0174532925f ) * m_backwardSpeed;

	return;
}

void DMPosition::MoveUpward( bool keydown )
{
	// Update the upward speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_upwardSpeed += m_frameTime * 0.0003f;

		if( m_upwardSpeed > ( m_frameTime * 0.003f ) )
		{
			m_upwardSpeed = m_frameTime * 0.003f;
		}
	}
	else
	{
		m_upwardSpeed -= m_frameTime * 0.0002f;

		if( m_upwardSpeed < 0.0f )
		{
			m_upwardSpeed = 0.0f;
		}
	}


	// Update the height position.
	m_positionY += m_upwardSpeed;

	return;
}

void DMPosition::MoveDownward( bool keydown )
{
	// Update the downward speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_downwardSpeed += m_frameTime * 0.0003f;

		if( m_downwardSpeed > ( m_frameTime * 0.003f ) )
		{
			m_downwardSpeed = m_frameTime * 0.003f;
		}
	}
	else
	{
		m_downwardSpeed -= m_frameTime * 0.0002f;

		if( m_downwardSpeed < 0.0f )
		{
			m_downwardSpeed = 0.0f;
		}
	}

	// Update the height position.
	m_positionY -= m_downwardSpeed;

	return;
}

void DMPosition::TurnLeft( bool keydown )
{
	// Update the left turn speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_leftTurnSpeed += m_frameTime * 0.01f;

		if( m_leftTurnSpeed > ( m_frameTime * 0.15f ) )
		{
			m_leftTurnSpeed = m_frameTime * 0.15f;
		}
	}
	else
	{
		m_leftTurnSpeed -= m_frameTime* 0.005f;

		if( m_leftTurnSpeed < 0.0f )
		{
			m_leftTurnSpeed = 0.0f;
		}
	}

	// Update the rotation.
	m_rotationY -= m_leftTurnSpeed;

	// Keep the rotation in the 0 to 360 range.
	if( m_rotationY < 0.0f )
	{
		m_rotationY += 360.0f;
	}

	return;
}

void DMPosition::TurnRight( bool keydown )
{
	// Update the right turn speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_rightTurnSpeed += m_frameTime * 0.01f;

		if( m_rightTurnSpeed > ( m_frameTime * 0.15f ) )
		{
			m_rightTurnSpeed = m_frameTime * 0.15f;
		}
	}
	else
	{
		m_rightTurnSpeed -= m_frameTime* 0.005f;

		if( m_rightTurnSpeed < 0.0f )
		{
			m_rightTurnSpeed = 0.0f;
		}
	}

	// Update the rotation.
	m_rotationY += m_rightTurnSpeed;

	// Keep the rotation in the 0 to 360 range.
	if( m_rotationY > 360.0f )
	{
		m_rotationY -= 360.0f;
	}

	return;
}

void DMPosition::LookUpward( bool keydown )
{
	// Update the upward rotation speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_lookUpSpeed += m_frameTime * 0.01f;

		if( m_lookUpSpeed > ( m_frameTime * 0.15f ) )
		{
			m_lookUpSpeed = m_frameTime * 0.15f;
		}
	}
	else
	{
		m_lookUpSpeed -= m_frameTime* 0.005f;

		if( m_lookUpSpeed < 0.0f )
		{
			m_lookUpSpeed = 0.0f;
		}
	}

	// Update the rotation.
	m_rotationX -= m_lookUpSpeed;

	// Keep the rotation maximum 90 degrees.
	if( m_rotationX > 90.0f )
	{
		m_rotationX = 90.0f;
	}

	return;
}

void DMPosition::LookDownward( bool keydown )
{
	// Update the downward rotation speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_lookDownSpeed += m_frameTime * 0.01f;

		if( m_lookDownSpeed > ( m_frameTime * 0.15f ) )
		{
			m_lookDownSpeed = m_frameTime * 0.15f;
		}
	}
	else
	{
		m_lookDownSpeed -= m_frameTime* 0.005f;

		if( m_lookDownSpeed < 0.0f )
		{
			m_lookDownSpeed = 0.0f;
		}
	}

	// Update the rotation.
	m_rotationX += m_lookDownSpeed;

	// Keep the rotation maximum 90 degrees.
	if( m_rotationX < -90.0f )
	{
		m_rotationX = -90.0f;
	}

	return;
}

void DMPosition::MoveLeft( bool keydown )
{
	// Update the upward speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_leftwardSpeed += m_frameTime * 0.0003f;

		if( m_leftwardSpeed > ( m_frameTime * 0.003f ) )
		{
			m_leftwardSpeed = m_frameTime * 0.003f;
		}
	}
	else
	{
		m_leftwardSpeed -= m_frameTime * 0.0002f;

		if( m_leftwardSpeed < 0.0f )
		{
			m_leftwardSpeed = 0.0f;
		}
	}
	


	// Convert degrees to radians.
	float radians = m_rotationY * 0.0174532925f;

	// Update the position.
	//m_positionY -= sinf( radians ) * m_rightwardSpeed;
	m_positionZ += sinf( radians ) * m_leftwardSpeed;
	m_positionX -= cosf( m_rotationY * 0.0174532925f ) * m_leftwardSpeed;


	return;
}

void DMPosition::MoveRight( bool keydown )
{
	// Update the downward speed movement based on the frame time and whether the user is holding the key down or not.
	if( keydown )
	{
		m_rightwardSpeed += m_frameTime * 0.0003f;

		if( m_rightwardSpeed > ( m_frameTime * 0.003f ) )
		{
			m_rightwardSpeed = m_frameTime * 0.003f;
		}
	}
	else
	{
		m_rightwardSpeed -= m_frameTime * 0.0002f;

		if( m_rightwardSpeed < 0.0f )
		{
			m_rightwardSpeed = 0.0f;
		}
	}

	// Update the height position.
	//m_positionX += m_rightwardSpeed;


	// Convert degrees to radians.
	float radians = m_rotationY * 0.0174532925f;

	// Update the position.
	//m_positionY -= sinf( radians ) * m_rightwardSpeed;
	m_positionZ -= sinf( radians ) * m_rightwardSpeed;
	m_positionX += cosf( m_rotationY * 0.0174532925f ) * m_rightwardSpeed;


	return;
}