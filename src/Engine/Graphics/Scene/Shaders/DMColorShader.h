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

	virtual void Shutdown();
	virtual void Update( float );

private:
	virtual void prepareRender();;
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts();

};

}