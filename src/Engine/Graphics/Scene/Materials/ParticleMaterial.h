#pragma once

#include "Material.h"

namespace GS
{

// Материал частиц (DMParticleSystem): точки по SV_VertexID, их разворачивает в карточки геометрический шейдер
class ParticleMaterial : public Material
{
public:
	ParticleMaterial( uint32_t id, const std::string& name );
	bool initialize() override;
};

}
