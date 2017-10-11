#pragma once
#include "DMParticleEmitter.h"

class DMPEShape : public DMParticleEmitter
{
public:
	DMPEShape();
	~DMPEShape();

	enum Shape
	{
		pe_shape_box, pe_shape_ellipse
	};

	void Initialize( unsigned int max_particles, Shape, DMAABB, float density );

private:
	Shape m_shape;
};

