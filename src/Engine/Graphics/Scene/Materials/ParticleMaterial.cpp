#include "ParticleMaterial.h"

namespace GS
{

ParticleMaterial::ParticleMaterial( uint32_t id, const std::string& name ) : Material( id, name )
{
}

bool ParticleMaterial::initialize()
{
	// Частицы — точки (SV_VertexID), их разворачивает геометрический шейдер: топология — часть пайплайна, без неё
	// прогрев собрал бы пайплайн с треугольниками и GS для точек его отвергнул бы
	setTopology( D3D_PRIMITIVE_TOPOLOGY_POINTLIST );
	return createPhase( 0, 0, 0 ) >= 0;
}

}
