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
	ID3D11Buffer* buffer;
	createPhase( 0, 0, 0 );
	return true;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMParticleShader::initLayouts()
{
	return std::vector<D3D11_INPUT_ELEMENT_DESC>();
}

void DMParticleShader::setParams( const ParamSet& params )
{
	
}

}