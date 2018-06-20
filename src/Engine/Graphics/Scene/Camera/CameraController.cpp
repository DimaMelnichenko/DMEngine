#include "CameraController.h"
#include "Engine\Input\Input.h"

CameraController::CameraController() : m_camera(nullptr)
{

}

CameraController::~CameraController()
{

}

void CameraController::setMoveCamera( DMCamera* camera )
{
	m_camera = camera;
}

void CameraController::frame( float elapsedTime )
{
	if( !m_camera )
		return;

	//update main camera position
	Input& input = getInput();

	static float cameraX = m_camera->position().x;
	static float cameraY = m_camera->position().y;
	static float cameraZ = m_camera->position().z;

	float offset = 0.01f * elapsedTime;

	if( input.IsForwarPressed() )
	{
		cameraZ += offset;
	}

	if( input.IsBackwardPressed() )
	{
		cameraZ -= offset;
	}

	if( input.IsRightStride() )
	{
		cameraX += offset;
	}

	if( input.IsLeftStride() )
	{
		cameraX -= offset;
	}

	if( input.IsUpMove() )
	{
		cameraY += offset;
	}

	if( input.IsDownMove() )
	{
		cameraY -= offset;
	}

	m_camera->SetPosition( cameraX, cameraY, cameraZ );

	double mouseX;
	double mouseY;

	input.GetMouseLocation( mouseX, mouseY );

	m_camera->SetRotation( mouseY * 0.1f, mouseX * 0.1f, 0.0f );
}