#pragma once

#include "DirectX.h"
#include "..\Shaders\DMShader.h"

namespace GS
{

class DMParticleShader : public DMShader
{
public:
	DMParticleShader();
	~DMParticleShader();

	bool Prepare();

private:
	bool innerInitialize() override;
	std::vector<VertexElement> initLayouts() override;
};

}
