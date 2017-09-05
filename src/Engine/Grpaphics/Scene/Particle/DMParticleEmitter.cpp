#include "DMParticleEmitter.h"



DMParticleEmitter::DMParticleEmitter( DMD3D* parent ) : DMSceneObject( parent )
{
}


DMParticleEmitter::~DMParticleEmitter()
{

}

bool DMParticleEmitter::Initialize( unsigned int max_particles )
{
	m_max_particles = max_particles;

	return true;
}

void DMParticleEmitter::Render()
{

}

void DMParticleEmitter::Update( float elapsed_time )
{

}

unsigned int DMParticleEmitter::vertexCount()
{
	return m_max_particles;
}