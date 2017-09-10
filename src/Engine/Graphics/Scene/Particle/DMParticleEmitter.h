#pragma once

#include "..\DMSceneObject.h"

class DMParticleEmitter : public DMSceneObject
{
public:
	DMParticleEmitter( DMD3D* );
	~DMParticleEmitter();

	bool Initialize( unsigned int max_particles );

	void Render();
	void Update( float elapsed_time );

	unsigned int vertexCount();

private:
	unsigned int m_max_particles;
};

