#pragma once

#include "DMParticleEmitter.h"


class DMClouds : public DMParticleEmitter
{
public:
	DMClouds( DMD3D* );
	~DMClouds();

	void Initialize();
};

