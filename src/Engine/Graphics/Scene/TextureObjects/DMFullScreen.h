#pragma once

#include "..\DMSceneObject.h"

class DMFullScreen : public DMSceneObject
{
public:
	DMFullScreen();
	~DMFullScreen();

	void Render();

	unsigned int vertexCount();
};

