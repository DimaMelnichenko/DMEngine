#pragma once

#include "Camera\DMCamera.h"


class CameraController
{
public:
	CameraController();
	~CameraController();

	void setMoveCamera( DMCamera* camera );
	void frame( float elapsedTime );

private:
	DMCamera* m_camera;
};