#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>
#include "DMShader.h"


class DMDepthShader : public DMShader
{
private:
	

public:
	DMDepthShader();
	~DMDepthShader();

private:
	virtual void prepareRender();
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
};

