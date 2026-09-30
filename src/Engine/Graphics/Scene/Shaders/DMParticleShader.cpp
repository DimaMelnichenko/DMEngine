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
	// Частицы — точки (SV_VertexID), их разворачивает геометрический шейдер: топология — часть пайплайна, без неё
	// прогрев собрал бы пайплайн с треугольниками и GS для точек его отвергнул бы
	setTopology( D3D_PRIMITIVE_TOPOLOGY_POINTLIST );
	return createPhase( 0, 0, 0 ) >= 0;
}

std::vector<VertexElement> DMParticleShader::initLayouts()
{
	return std::vector<VertexElement>();
}

}
