#pragma once

#include "..\Shaders\DMShader.h"

class DMFullScreenShader : public DMShader
{
public:
	DMFullScreenShader( DMD3D* );
	~DMFullScreenShader();

	void Update( float );

private:
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void prepareRender();
};

