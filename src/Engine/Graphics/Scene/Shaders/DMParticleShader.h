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
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();
};

}