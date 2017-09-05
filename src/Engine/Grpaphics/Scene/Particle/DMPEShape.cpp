
#include "DMPEShape.h"
#include <random>


DMPEShape::DMPEShape( DMD3D* parent ) : DMParticleEmitter( parent )
{

}


DMPEShape::~DMPEShape()
{

}

void DMPEShape::Initialize( unsigned int max_particles, Shape shape, DMAABB aabb, float density )
{
	DMParticleEmitter::Initialize( max_particles );

	m_shape = shape;

	std::mt19937 gen( 0 );

	std::uniform_real_distribution<> urd_x( 0, 100 );
	std::uniform_real_distribution<> urd_y( 0, 100 );
	std::uniform_real_distribution<> urd_z( 0, 100 );


}
