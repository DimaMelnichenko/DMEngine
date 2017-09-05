#pragma once

#include "DMSceneObject.h"

class DMFullScreen : public DMSceneObject
{
public:
	DMFullScreen( DMD3D* );
	~DMFullScreen();

	void Render();

	unsigned int vertexCount();
};

