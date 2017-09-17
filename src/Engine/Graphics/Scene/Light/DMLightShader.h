#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <fstream>

#include "..\Model\DMModel.h"
#include "DMLightDriver.h"
#include "..\Shaders\DMShader.h"

class DMLightShader : public DMShader
{
public:
	

public:
	DMLightShader( DMD3D* );
	~DMLightShader();

	bool setTexture( DMModel::TextureType, ID3D11ShaderResourceView* );
	bool setCubeTexture( ID3D11ShaderResourceView* );
	bool setLights( DMLightDriver* );	

private:
	void Shutdown();
	void prepareRender(  );
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

};

