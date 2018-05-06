#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>
#include "DMShader.h"

namespace GS
{

class DMColorShader : public DMShader
{
private:

public:
	DMColorShader();
	~DMColorShader();

	bool Prepare();

private:
	bool innerInitialize() override;
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

};

}