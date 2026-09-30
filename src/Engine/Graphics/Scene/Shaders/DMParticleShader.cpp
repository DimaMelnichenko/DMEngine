#include "DMParticleShader.h"

namespace GS
{

DMParticleShader::DMParticleShader()
{

}

DMParticleShader::~DMParticleShader()
{
}

bool DMParticleShader::innerInitialize()
{
	return createPhase( 0, 0, 0 ) >= 0;
}

std::vector<VertexElement> DMParticleShader::initLayouts()
{
	return std::vector<VertexElement>();
}

}
