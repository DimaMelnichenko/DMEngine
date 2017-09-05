#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include <d3dx11async.h>
#include <fstream>
#include "DMShader.h"
#include "DMLightDriver.h"
#include "DMModel.h"

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

