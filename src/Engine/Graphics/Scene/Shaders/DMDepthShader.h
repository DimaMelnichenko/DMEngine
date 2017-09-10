#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include <d3dx11async.h>
#include <fstream>
#include "DMShader.h"


class DMDepthShader : public DMShader
{
private:
	

public:
	DMDepthShader( DMD3D* );
	~DMDepthShader();

private:
	virtual void prepareRender();
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
};

