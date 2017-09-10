#pragma once

//////////////
// INCLUDES //
//////////////

#include "DMShader.h"

class DMShadowShader : public DMShader
{
public:
	DMShadowShader( DMD3D* );
	~DMShadowShader();

private:
	void prepareRender();
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

};

